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

static void write_conf(void)
{
    FILE *fp;

    snprintf(conf_path, sizeof(conf_path), "%s/aept.conf", root);
    fp = fopen(conf_path, "w");
    if (!fp) {
        perror(conf_path);
        exit(2);
    }
    /* Enough that loading it allocates dozens of times. */
    fprintf(fp, "option lists_dir %s/lists\n", root);
    fprintf(fp, "option info_dir %s/info\n", root);
    fprintf(fp, "option cache_dir %s/cache\n", root);
    fprintf(fp, "option tmp_dir %s/tmp\n", root);
    fprintf(fp, "option lock_file %s/lock\n", root);
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

/* Fail the n-th allocation of aept_load_config() for every n, until an n
 * the call never reaches.  Returns how many it makes. */
static long sweep_load_config(void)
{
    long n;

    for (n = 1; n <= 5000; n++) {
        aept_ctx_t *ctx = aept_init();
        int r;

        if (!ctx) {
            test_ok(0, "aept_init() returned NULL outside injection");
            return n;
        }
        aept_set_log_fn(ctx, quiet_log, NULL);

        arm(n);
        r = aept_load_config(ctx, conf_path);
        disarm();

        if (!injected) {
            /* Fewer than n allocations: the sweep is done, and the call
             * must therefore have succeeded. */
            test_int_eq(r, 0, "aept_load_config() succeeds once injection stops biting");
            aept_cleanup(ctx);
            return n - 1;
        }

        if (r != -1) {
            printf("#   allocation %ld failed but the call returned %d\n", n, r);
            test_ok(0, "every injected allocation failure returns -1");
            aept_cleanup(ctx);
            return n;
        }
        if (aept_last_error(ctx) != AEPT_ERR_NOMEM) {
            printf("#   allocation %ld: last_error is %d, want %d\n", n, aept_last_error(ctx),
                   AEPT_ERR_NOMEM);
            test_ok(0, "every injected allocation failure reports AEPT_ERR_NOMEM");
            aept_cleanup(ctx);
            return n;
        }

        /* The half-built config is leaked by design: longjmp() runs no
         * cleanup.  aept_cleanup() releases what it can still see. */
        aept_cleanup(ctx);
    }

    test_ok(0, "sweep did not terminate");
    return -1;
}

int main(void)
{
    aept_ctx_t *ctx;
    long depth;
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

    /* The sweep: every allocation, one at a time. */
    depth = sweep_load_config();
    printf("# aept_load_config() allocates %ld times\n", depth);
    test_ok(depth > 20, "the sweep covered a meaningful number of allocations");

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

    /* Still here. */
    test_ok(1, "the process survived every injected failure");

    return test_summary();
}
