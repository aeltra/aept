/* test_oom.c - an allocation failure returns an error instead of ending
 * the process
 *
 * What has to be proved is that this holds for *every* allocation a call
 * makes, not just the first: a failure at allocation 40 must land where
 * one at allocation 1 does.  So the allocators are wrapped (-Wl,--wrap,
 * see tests/Makefile.am) and the n-th allocation is failed for every n
 * until the call stops allocating.  Each iteration must return -1, report
 * AEPT_ERR_NOMEM, and leave a process still running to be asked -- which
 * is what fails without the fix, since exit() takes the test binary with
 * it and the harness sees a status rather than a plan.
 *
 * Every public entry point that arms the escape is swept, not just one.
 * Driving a single function left the other twenty with their
 * AEPT_OOM_ENTER/LEAVE pair half-covered: the arming side exercised and
 * the returning side -- the side that matters -- never reached.
 *
 * The sweeps are shallow for the transaction calls, because this fixture
 * has no packages and no network: install and remove bail out after a
 * handful of allocations.  What that still proves is the contract at the
 * entry point.  Driving them deeper needs the shell fixtures, and is a
 * different exercise.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aept/aept.h"

#include "test.h"

/* ── the fault injector ──────────────────────────────────────────────── */

void *__real_malloc(size_t size);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *ptr, size_t size);
char *__real_strdup(const char *s);
int __real_vasprintf(char **strp, const char *fmt, va_list ap);

static long alloc_seen; /* allocations since arming */
static long fail_on;    /* which one to fail; 0 = fail none */
static int injected;    /* whether the injected failure actually happened */

static int trip(void)
{
    if (!fail_on)
        return 0;
    if (++alloc_seen != fail_on)
        return 0;
    injected = 1;
    return 1;
}

static void arm(long n)
{
    alloc_seen = 0;
    fail_on = n;
    injected = 0;
}

static void disarm(void)
{
    fail_on = 0;
}

void *__wrap_malloc(size_t size)
{
    return trip() ? NULL : __real_malloc(size);
}

/* The compiler emits calls the source never wrote: at -O2 gcc rewrites
 * malloc()+memset(0) into calloc(), which is what aept_init() is. */
void *__wrap_calloc(size_t n, size_t size)
{
    return trip() ? NULL : __real_calloc(n, size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    return trip() ? NULL : __real_realloc(ptr, size);
}

char *__wrap_strdup(const char *s)
{
    return trip() ? NULL : __real_strdup(s);
}

int __wrap_vasprintf(char **strp, const char *fmt, va_list ap)
{
    return trip() ? -1 : __real_vasprintf(strp, fmt, ap);
}

/* ── fixture ─────────────────────────────────────────────────────────── */

static char dir_template[] = "/tmp/aept-oom-XXXXXX";
static char *root;
static char conf_path[512];
static int root_seq;

/*
 * A fresh root for every call under test.  An abandoned call leaves its
 * partial state behind -- longjmp() runs no cleanup, which the contract
 * says plainly -- and a lock file left by iteration n changes what
 * iteration n+1 returns.  Sharing one root made the sweep depend on its
 * own history.
 */
static void write_conf(void)
{
    FILE *fp;
    char dir[512];

    snprintf(dir, sizeof(dir), "%s/r%d", root, ++root_seq);
    if (mkdir(dir, 0755) < 0) {
        perror(dir);
        exit(2);
    }
    snprintf(conf_path, sizeof(conf_path), "%s/aept.conf", dir);
    fp = fopen(conf_path, "w");
    if (!fp) {
        perror(conf_path);
        exit(2);
    }
    /* Enough that loading it allocates dozens of times. */
    fprintf(fp, "option lists_dir %s/lists\n", dir);
    fprintf(fp, "option info_dir %s/info\n", dir);
    fprintf(fp, "option cache_dir %s/cache\n", dir);
    fprintf(fp, "option tmp_dir %s/tmp\n", dir);
    fprintf(fp, "option lock_file %s/lock\n", dir);
    fprintf(fp, "option check_signature 0\n");
    fprintf(fp, "arch all 1\n");
    fprintf(fp, "arch x86_64 10\n");
    fprintf(fp, "src/gz alpha http://example.invalid/alpha\n");
    fprintf(fp, "src/gz beta http://example.invalid/beta\n");
    fprintf(fp, "src/gz gamma http://example.invalid/gamma\n");
    fclose(fp);
}

/* Logging is noise here: every injected failure is expected. */
static void quiet_log(int level, const char *msg, void *userdata)
{
    (void)level;
    (void)msg;
    (void)userdata;
}

/*
 * One call under test.  Everything it needs beyond a loaded config is
 * its own business; the sweep supplies a fresh context each time,
 * because a call abandoned by longjmp() leaves state behind by design.
 */
typedef int (*oom_call_fn)(aept_ctx_t *ctx);

/*
 * Fail the n-th allocation of one call for every n, until an n the call
 * never reaches.  Returns how many allocations it makes.
 *
 * expect is what the call returns with nothing injected.  Not every
 * entry point succeeds against this fixture -- there are no packages and
 * no network, so several fail on their own merits -- and that is fine:
 * what is under test is that an allocation failure returns and reports
 * AEPT_ERR_NOMEM, not that the operation would have worked.
 */
static long sweep(const char *what, oom_call_fn call, int expect, int load_config)
{
    long n, nomem = 0;

    for (n = 1; n <= 5000; n++) {
        aept_ctx_t *ctx;
        int r;

        write_conf();
        ctx = aept_init();
        if (!ctx) {
            test_ok(0, "aept_init() returned NULL outside injection");
            return n;
        }
        aept_set_log_fn(ctx, quiet_log, NULL);

        if (load_config && aept_load_config(ctx, conf_path) != 0) {
            test_ok(0, "the fixture config failed to load");
            aept_cleanup(ctx);
            return n;
        }

        arm(n);
        r = call(ctx);
        disarm();

        if (!injected) {
            /* Fewer than n allocations: the sweep is done, and the call
             * must have behaved as it does uninjected. */
            if (r != expect)
                printf("#   %s: uninjected return %d, expected %d\n", what, r, expect);
            test_ok(r == expect, "the call behaves as it does uninjected once injection stops");
            aept_cleanup(ctx);
            if (n > 1)
                printf("# %-22s %ld of %ld failures reported NOMEM\n", what, nomem, n - 1);
            return n - 1;
        }

        /*
         * Two acceptable answers, and one that is not.
         *
         * The wrapper intercepts every allocation in aept's own objects,
         * which is more than the aept_*() allocators: a site that calls
         * malloc() directly and checks the result may handle the failure
         * itself, and answering as it does uninjected is correct.  What
         * is not allowed is reporting failure the caller cannot
         * classify -- a -1 with no error to read.
         */
        if (r != -1) {
            if (r != expect)
                printf("#   %s: allocation %ld failed, returned %d, uninjected gives %d\n", what, n,
                       r, expect);
            test_ok(r == expect,
                    "a handled allocation failure answers as the call does uninjected");
            aept_cleanup(ctx);
            if (r != expect)
                return n;
            continue;
        }
        /*
         * AEPT_ERR_NOMEM is what the escape sets, and api.c uses the
         * aept_*() allocators throughout, so an injected allocation
         * failure that reaches the caller as -1 must carry it -- or a
         * more specific code a site set on the way.  A -1 with nothing
         * set is not wrong in general (aept.h documents it for a failure
         * with no classification); here it means a plain malloc() has
         * crept back into api.c and its failure never reached the
         * escape.  That is how api_architectures() was found.
         */
        if (aept_last_error(ctx) == AEPT_ERR_NONE) {
            printf("#   %s: allocation %ld failed, returned -1, last_error is NONE\n", what, n);
            test_ok(0, "a reported failure leaves an error to read");
            aept_cleanup(ctx);
            return n;
        }
        if (aept_last_error(ctx) == AEPT_ERR_NOMEM)
            nomem++;

        /* The half-built state is leaked by design: longjmp() runs no
         * cleanup.  aept_cleanup() releases what it can still see. */
        aept_cleanup(ctx);
    }

    test_ok(0, "sweep did not terminate");
    return -1;
}

/* ── the calls, one thunk each ───────────────────────────────────────── */

static const char *one_name[] = {"nosuchpkg"};

static int call_load_config(aept_ctx_t *c)
{
    return aept_load_config(c, conf_path);
}
static int call_set_offline_root(aept_ctx_t *c)
{
    return aept_set_offline_root(c, "/nonexistent");
}
static int call_set_cache_dir(aept_ctx_t *c)
{
    return aept_set_cache_dir(c, "/nonexistent");
}
static int call_update(aept_ctx_t *c)
{
    return aept_update(c);
}
static int call_upgrade(aept_ctx_t *c)
{
    return aept_upgrade(c);
}
static int call_autoremove(aept_ctx_t *c)
{
    return aept_autoremove(c);
}
static int call_clean(aept_ctx_t *c)
{
    return aept_clean(c);
}
static int call_triggers(aept_ctx_t *c)
{
    return aept_triggers(c);
}
static int call_mark_manual_all(aept_ctx_t *c)
{
    return aept_mark_manual_all(c);
}
static int call_install(aept_ctx_t *c)
{
    return aept_install(c, one_name, 1, NULL, 0);
}
static int call_remove(aept_ctx_t *c)
{
    return aept_remove(c, one_name, 1);
}
static int call_mark_auto(aept_ctx_t *c)
{
    return aept_mark_auto(c, one_name, 1);
}
static int call_mark_manual(aept_ctx_t *c)
{
    return aept_mark_manual(c, one_name, 1);
}
static int call_pin(aept_ctx_t *c)
{
    return aept_pin(c, one_name, 1);
}
static int call_unpin(aept_ctx_t *c)
{
    return aept_unpin(c, one_name, 1);
}

static int call_list(aept_ctx_t *c)
{
    aept_pkg_list_t out;
    int r = aept_list(c, NULL, 0, 0, &out);

    if (r == 0)
        aept_pkg_list_free(&out);
    return r;
}

static int call_show(aept_ctx_t *c)
{
    aept_pkg_info_t out;
    int r = aept_show(c, "nosuchpkg", &out);

    if (r == 0)
        aept_pkg_info_free(&out);
    return r;
}

static int call_show_all(aept_ctx_t *c)
{
    aept_pkg_info_list_t out;
    int r = aept_show_all(c, "nosuchpkg", &out);

    if (r == 0)
        aept_pkg_info_list_free(&out);
    return r;
}

/* The string-array calls hand back a malloc'd vector of malloc'd
 * strings; there is no helper for it, so free it the way the CLI does. */
static void free_strings(char **v, int n)
{
    int i;

    for (i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static int call_files(aept_ctx_t *c)
{
    char **paths = NULL;
    int n = 0, r = aept_files(c, "nosuchpkg", &paths, &n);

    if (r == 0)
        free_strings(paths, n);
    return r;
}

static int call_owns(aept_ctx_t *c)
{
    char **owners = NULL;
    int n = 0, r = aept_owns(c, "/bin/sh", &owners, &n);

    if (r == 0)
        free_strings(owners, n);
    return r;
}

static int call_architectures(aept_ctx_t *c)
{
    char **archs = NULL;
    int n = 0, r = aept_architectures(c, &archs, &n);

    if (r == 0)
        free_strings(archs, n);
    return r;
}

/*
 * Every entry point that arms the escape.  The uninjected return is
 * recorded rather than assumed: against this fixture some of these
 * succeed and some fail, and which is which is not the point.
 */
static const struct {
    const char *name;
    oom_call_fn call;
    int load_config;
} CALLS[] = {
    {"aept_set_offline_root", call_set_offline_root, 0},
    {"aept_set_cache_dir",    call_set_cache_dir,    0},
    {"aept_load_config",      call_load_config,      0},
    {"aept_architectures",    call_architectures,    1},
    {"aept_list",             call_list,             1},
    {"aept_show",             call_show,             1},
    {"aept_show_all",         call_show_all,         1},
    {"aept_files",            call_files,            1},
    {"aept_owns",             call_owns,             1},
    {"aept_clean",            call_clean,            1},
    {"aept_triggers",         call_triggers,         1},
    {"aept_autoremove",       call_autoremove,       1},
    {"aept_upgrade",          call_upgrade,          1},
    {"aept_update",           call_update,           1},
    {"aept_install",          call_install,          1},
    {"aept_remove",           call_remove,           1},
    {"aept_mark_auto",        call_mark_auto,        1},
    {"aept_mark_manual",      call_mark_manual,      1},
    {"aept_mark_manual_all",  call_mark_manual_all,  1},
    {"aept_pin",              call_pin,              1},
    {"aept_unpin",            call_unpin,            1},
};

/* What the call does with nothing injected, so the sweep knows when it
 * has run past the end of the allocations. */
static int uninjected(oom_call_fn call, int load_config)
{
    aept_ctx_t *ctx;
    int r;

    write_conf();
    ctx = aept_init();
    aept_set_log_fn(ctx, quiet_log, NULL);
    if (load_config)
        aept_load_config(ctx, conf_path);
    r = call(ctx);
    aept_cleanup(ctx);
    return r;
}

int main(void)
{
    aept_ctx_t *ctx;
    long depth, config_depth = 0;
    const char *before;

    root = mkdtemp(dir_template);
    if (!root) {
        perror("mkdtemp");
        return 2;
    }
    write_conf();

    /* Positive control: with nothing injected the fixture loads. */
    ctx = aept_init();
    test_ok(ctx != NULL, "aept_init() succeeds with no injection");
    aept_set_log_fn(ctx, quiet_log, NULL);
    test_int_eq(aept_load_config(ctx, conf_path), 0, "aept_load_config() succeeds uninjected");
    aept_cleanup(ctx);

    /* aept_init() has no context to unwind to, so it reports NULL. */
    arm(1);
    ctx = aept_init();
    disarm();
    test_ok(ctx == NULL, "aept_init() returns NULL rather than exiting");
    aept_cleanup(ctx); /* NULL-safe */

    /*
     * The sweeps: every entry point that arms the escape, every
     * allocation within it, one at a time.  Driving only one of them
     * left the other twenty with their AEPT_OOM_ENTER/LEAVE pair
     * half-covered -- the arming side exercised and the returning side
     * never reached, which is the side that matters.
     */
    {
        size_t i;
        long total = 0;

        for (i = 0; i < sizeof(CALLS) / sizeof(CALLS[0]); i++) {
            int expect = uninjected(CALLS[i].call, CALLS[i].load_config);
            long n = sweep(CALLS[i].name, CALLS[i].call, expect, CALLS[i].load_config);

            printf("# %-22s allocates %4ld times (uninjected returns %d)\n", CALLS[i].name, n,
                   expect);
            if (n < 0)
                break;
            if (strcmp(CALLS[i].name, "aept_load_config") == 0)
                config_depth = n;
            total += n;
        }
        depth = total;
    }
    /*
     * A guard against the fixture silently breaking: if the config
     * stopped loading, every sweep would bottom out at nought and every
     * assertion above would pass vacuously.
     *
     * It asks about aept_load_config() and not the total, because the
     * total is not a property of the code.  How many allocations a call
     * makes depends on the build: -O2 with _FORTIFY_SOURCE inlines or
     * redirects enough of the libc calls that the wrapper never sees
     * them, and the same tree swept 105 allocations here against 76
     * under dpkg-buildpackage.  aept_load_config() goes entirely
     * through aept's own allocators, which are real calls into libaept
     * whatever the flags, and it came to 26 in both.
     */
    printf("# %ld allocations swept across %zu entry points\n", depth,
           sizeof(CALLS) / sizeof(CALLS[0]));
    test_ok(config_depth > 10, "the config sweep reached the fixture's allocations");

    /* The two allocating setters return their failure: aept_last_error()
     * is documented for calls that returned non-zero and is reset by
     * aept_download().  The previous value must survive, not dangle. */
    ctx = aept_init();
    aept_set_log_fn(ctx, quiet_log, NULL);
    test_int_eq(aept_set_cache_dir(ctx, "/first"), 0, "an uninjected setter returns 0");
    before = "/first";
    arm(1);
    test_int_eq(aept_set_cache_dir(ctx, "/second"), -1, "a failed setter returns -1");
    disarm();
    test_ok(injected, "the setter did allocate, so the injection was real");
    test_int_eq(aept_last_error(ctx), AEPT_ERR_NOMEM, "a failed setter reports AEPT_ERR_NOMEM");
    /* Using it again proves the old value was kept, not freed: a
     * dangling pointer here would be a use after free. */
    test_int_eq(aept_set_cache_dir(ctx, before), 0, "the setter works again after a failure");
    test_int_eq(aept_load_config(ctx, conf_path), 0, "the context still works afterwards");
    aept_cleanup(ctx);

    /*
     * A classification belongs to the call that produced it.  Fail one
     * call so AEPT_ERR_NOMEM is set, then make a second call that
     * succeeds: the second must not still be carrying the first one's
     * answer, or a caller asking after it would act on a condition that
     * happened to something else.
     */
    ctx = aept_init();
    aept_set_log_fn(ctx, quiet_log, NULL);
    write_conf();
    arm(1);
    test_int_eq(aept_set_cache_dir(ctx, "/somewhere"), -1, "the first call fails");
    disarm();
    test_int_eq(aept_last_error(ctx), AEPT_ERR_NOMEM, "and reports why");
    test_int_eq(aept_load_config(ctx, conf_path), 0, "a later call succeeds");
    test_int_eq(aept_last_error(ctx), AEPT_ERR_NONE,
                "and does not carry the earlier call's classification");
    aept_cleanup(ctx);

    /* Still here. */
    test_ok(1, "the process survived every injected failure");

    return test_summary();
}
