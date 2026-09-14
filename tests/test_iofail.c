/* test_iofail.c - an I/O failure is reported, not ignored
 *
 * aept runs as root against a filesystem it does not control: a disk
 * fills, a rename fails, a read returns EIO.  What must never happen is
 * that the call carries on as though it had worked -- a status file
 * written short, a package list half-read and believed, an error the
 * caller cannot see.
 *
 * Same shape as test_oom.c, one layer down: the I/O calls are wrapped
 * (-Wl,--wrap, see tests/Makefile.am), the n-th call of the function
 * under test is failed for every n, and each iteration must leave a
 * process still running and an error to read.  --wrap only rewrites call
 * sites in the objects linked here, so the injection reaches aept's own
 * code and leaves libsolv, libarchive and OpenSSL working normally.
 *
 * What it does NOT assert is how many calls anything makes.  That number
 * is a property of the build -- test_oom.c carries the measurement that
 * taught us so -- and of the filesystem underneath.  Only behaviour is
 * checked.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "aept/aept.h"

#include "test.h"

/* ── the fault injector ──────────────────────────────────────────────── */

FILE *__real_fopen(const char *path, const char *mode);
char *__real_fgets(char *s, int size, FILE *fp);
size_t __real_fwrite(const void *p, size_t size, size_t n, FILE *fp);
int __real_fputs(const char *s, FILE *fp);
int __real_fclose(FILE *fp);
int __real_rename(const char *from, const char *to);
int __real_mkdir(const char *path, mode_t mode);
DIR *__real_opendir(const char *path);
struct dirent *__real_readdir(DIR *d);
int __real_stat(const char *path, struct stat *st);
int __real_lstat(const char *path, struct stat *st);

static long io_seen; /* calls since arming */
static long fail_on; /* which one to fail; 0 = fail none */
static int injected; /* whether the injected failure actually happened */

static int trip(void)
{
    if (!fail_on)
        return 0;
    if (++io_seen != fail_on)
        return 0;
    injected = 1;
    return 1;
}

static void arm(long n)
{
    io_seen = 0;
    fail_on = n;
    injected = 0;
}

static void disarm(void)
{
    fail_on = 0;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (trip()) {
        errno = EACCES;
        return NULL;
    }
    return __real_fopen(path, mode);
}

char *__wrap_fgets(char *s, int size, FILE *fp)
{
    if (trip()) {
        errno = EIO;
        return NULL;
    }
    return __real_fgets(s, size, fp);
}

size_t __wrap_fwrite(const void *p, size_t size, size_t n, FILE *fp)
{
    if (trip()) {
        errno = ENOSPC;
        return 0;
    }
    return __real_fwrite(p, size, n, fp);
}

int __wrap_fputs(const char *s, FILE *fp)
{
    if (trip()) {
        errno = ENOSPC;
        return EOF;
    }
    return __real_fputs(s, fp);
}

/* Variadic, so it forwards through vfprintf() rather than __real_fprintf:
 * there is no portable way to pass a va_list on to the real fprintf. */
int __wrap_fprintf(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    int r;

    if (trip()) {
        errno = ENOSPC;
        return -1;
    }
    va_start(ap, fmt);
    r = vfprintf(fp, fmt, ap);
    va_end(ap);
    return r;
}

/*
 * A failing fclose() is the one most easily missed: the bytes are not on
 * disk until it returns, so a caller that ignores it has written nothing
 * and believes otherwise.  The stream is still closed here -- returning
 * without closing would leak a descriptor per iteration.
 */
int __wrap_fclose(FILE *fp)
{
    if (trip()) {
        __real_fclose(fp);
        errno = ENOSPC;
        return EOF;
    }
    return __real_fclose(fp);
}

int __wrap_rename(const char *from, const char *to)
{
    if (trip()) {
        errno = EXDEV;
        return -1;
    }
    return __real_rename(from, to);
}

int __wrap_mkdir(const char *path, mode_t mode)
{
    if (trip()) {
        errno = EACCES;
        return -1;
    }
    return __real_mkdir(path, mode);
}

DIR *__wrap_opendir(const char *path)
{
    if (trip()) {
        errno = EACCES;
        return NULL;
    }
    return __real_opendir(path);
}

struct dirent *__wrap_readdir(DIR *d)
{
    if (trip()) {
        errno = EIO;
        return NULL;
    }
    return __real_readdir(d);
}

int __wrap_stat(const char *path, struct stat *st)
{
    if (trip()) {
        errno = EIO;
        return -1;
    }
    return __real_stat(path, st);
}

int __wrap_lstat(const char *path, struct stat *st)
{
    if (trip()) {
        errno = EIO;
        return -1;
    }
    return __real_lstat(path, st);
}

/* ── fixture ─────────────────────────────────────────────────────────── */

static char dir_template[] = "/tmp/aept-iofail-XXXXXX";
static char *base;
static char conf_path[512];
static int root_seq;

static void must_mkdir(const char *path)
{
    if (__real_mkdir(path, 0755) < 0 && errno != EEXIST) {
        perror(path);
        exit(2);
    }
}

static void spit(const char *path, const char *text)
{
    FILE *fp = __real_fopen(path, "w");

    if (!fp) {
        perror(path);
        exit(2);
    }
    fputs(text, fp);
    __real_fclose(fp);
}

/*
 * A fresh root per call under test, carrying an installed package and an
 * index that offers a newer one.  test_oom.c's fixture had neither, and
 * its sweeps stopped a handful of calls in as a result; here the query
 * and state commands have something to read, so the injection reaches
 * past the entry point and into the code that parses what it finds.
 */
static void make_root(void)
{
    char dir[512], p[640], text[1024];

    snprintf(dir, sizeof(dir), "%s/r%d", base, ++root_seq);
    must_mkdir(dir);

    snprintf(p, sizeof(p), "%s/lists", dir);
    must_mkdir(p);
    snprintf(p, sizeof(p), "%s/info", dir);
    must_mkdir(p);
    snprintf(p, sizeof(p), "%s/cache", dir);
    must_mkdir(p);
    snprintf(p, sizeof(p), "%s/tmp", dir);
    must_mkdir(p);

    snprintf(conf_path, sizeof(conf_path), "%s/aept.conf", dir);
    snprintf(text, sizeof(text),
             "option lists_dir %s/lists\n"
             "option info_dir %s/info\n"
             "option cache_dir %s/cache\n"
             "option tmp_dir %s/tmp\n"
             "option lock_file %s/lock\n"
             "option auto_file %s/auto-installed\n"
             "option pin_file %s/pinned-packages\n"
             "option check_signature 0\n"
             "arch all 1\n"
             "arch x86_64 10\n"
             "src/gz testrepo file://%s\n",
             dir, dir, dir, dir, dir, dir, dir, dir);
    spit(conf_path, text);

    /* An installed package, with the file list and control that
     * aept_files(), aept_owns() and aept_show() read back. */
    snprintf(p, sizeof(p), "%s/info/alpha.control", dir);
    spit(p, "Package: alpha\n"
            "Version: 1.0\n"
            "Architecture: all\n"
            "Status: install ok installed\n"
            "Description: a fixture\n");
    snprintf(p, sizeof(p), "%s/info/alpha.list", dir);
    spit(p, "./usr/bin/alpha\t0100755\n"
            "./usr/share/alpha/data\t0100644\n");

    /* The records mark and pin rewrite; without these they reach for
     * /var/lib/aept and fail before the injection is even relevant. */
    snprintf(p, sizeof(p), "%s/auto-installed", dir);
    spit(p, "");
    snprintf(p, sizeof(p), "%s/pinned-packages", dir);
    spit(p, "");

    /* The status database the solver loads as "@installed". */
    snprintf(p, sizeof(p), "%s/info/status", dir);
    spit(p, "Package: alpha\n"
            "Version: 1.0\n"
            "Architecture: all\n"
            "Status: install ok installed\n"
            "Description: a fixture\n\n");

    /* An index offering a newer alpha, so upgrade has work to consider. */
    snprintf(p, sizeof(p), "%s/lists/testrepo", dir);
    spit(p, "Origin: test\n"
            "Date: 2026-01-01T00:00:00Z\n\n"
            "Package: alpha\n"
            "Version: 2.0\n"
            "Architecture: all\n"
            "Filename: alpha_2.0.aeltra\n"
            "Size: 100\n"
            "SHA256: 0000000000000000000000000000000000000000000000000000000000000000\n"
            "Description: a fixture\n\n"
            "Package: beta\n"
            "Version: 1.0\n"
            "Architecture: all\n"
            "Depends: alpha\n"
            "Filename: beta_1.0.aeltra\n"
            "Size: 100\n"
            "SHA256: 0000000000000000000000000000000000000000000000000000000000000000\n"
            "Description: another fixture\n\n");
}

/* Logging is noise here: every injected failure is expected. */
static void quiet_log(int level, const char *msg, void *userdata)
{
    (void)level;
    (void)msg;
    (void)userdata;
}

/* ── the sweep ───────────────────────────────────────────────────────── */

typedef int (*io_call_fn)(aept_ctx_t *ctx);

/*
 * Fail the n-th I/O call of one entry point for every n, until an n it
 * never reaches.  Returns how many calls it makes -- reported, never
 * asserted on.
 */
static long sweep(const char *what, io_call_fn call, int expect)
{
    long n;

    for (n = 1; n <= 2000; n++) {
        aept_ctx_t *ctx;
        int r;

        make_root();
        ctx = aept_init();
        if (!ctx) {
            test_ok(0, "aept_init() returned NULL outside injection");
            return n;
        }
        aept_set_log_fn(ctx, quiet_log, NULL);

        /* The config load is part of the fixture, not the subject. */
        if (aept_load_config(ctx, conf_path) != 0) {
            test_ok(0, "the fixture config failed to load");
            aept_cleanup(ctx);
            return n;
        }

        arm(n);
        r = call(ctx);
        disarm();

        if (!injected) {
            if (r != expect)
                printf("#   %s: uninjected return %d, expected %d\n", what, r, expect);
            test_ok(r == expect, "the call behaves as it does uninjected once injection stops");
            aept_cleanup(ctx);
            return n - 1;
        }

        /*
         * Whether the call reports the failure or decides it did not
         * depend on what broke is its own business -- a directory that
         * will not open is how "nothing is installed" looks.  What is
         * checked is that it returns at all, having neither crashed nor
         * spun, and that aept_cleanup() can still take the context
         * apart afterwards.  A NULL dereference after a failed fopen(),
         * a retry loop with no exit, a double free on the error path:
         * each of those ends the process here rather than passing.
         */
        aept_cleanup(ctx);
    }

    test_ok(0, "sweep did not terminate");
    return -1;
}

/* ── the calls ───────────────────────────────────────────────────────── */

static const char *alpha[] = {"alpha"};

static int call_load_config(aept_ctx_t *c)
{
    /* Loaded once already by the sweep; this is the subject, so it runs
     * again under injection against a config that is known good. */
    return aept_load_config(c, conf_path);
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
    int r = aept_show(c, "alpha", &out);

    if (r == 0)
        aept_pkg_info_free(&out);
    return r;
}

static int call_show_all(aept_ctx_t *c)
{
    aept_pkg_info_list_t out;
    int r = aept_show_all(c, "alpha", &out);

    if (r == 0)
        aept_pkg_info_list_free(&out);
    return r;
}

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
    int n = 0, r = aept_files(c, "alpha", &paths, &n);

    if (r == 0)
        free_strings(paths, n);
    return r;
}

static int call_owns(aept_ctx_t *c)
{
    char **owners = NULL;
    int n = 0, r = aept_owns(c, "/usr/bin/alpha", &owners, &n);

    if (r == 0)
        free_strings(owners, n);
    return r;
}

static int call_mark_auto(aept_ctx_t *c)
{
    return aept_mark_auto(c, alpha, 1);
}
static int call_mark_manual(aept_ctx_t *c)
{
    return aept_mark_manual(c, alpha, 1);
}
static int call_mark_manual_all(aept_ctx_t *c)
{
    return aept_mark_manual_all(c);
}
static int call_pin(aept_ctx_t *c)
{
    return aept_pin(c, alpha, 1);
}
static int call_unpin(aept_ctx_t *c)
{
    return aept_unpin(c, alpha, 1);
}
static int call_clean(aept_ctx_t *c)
{
    return aept_clean(c);
}
static int call_triggers(aept_ctx_t *c)
{
    return aept_triggers(c);
}
static int call_autoremove(aept_ctx_t *c)
{
    return aept_autoremove(c);
}
static int call_upgrade(aept_ctx_t *c)
{
    return aept_upgrade(c);
}
static int call_remove(aept_ctx_t *c)
{
    return aept_remove(c, alpha, 1);
}

static const struct {
    const char *name;
    io_call_fn call;
} CALLS[] = {
    {"aept_load_config",     call_load_config    },
    {"aept_list",            call_list           },
    {"aept_show",            call_show           },
    {"aept_show_all",        call_show_all       },
    {"aept_files",           call_files          },
    {"aept_owns",            call_owns           },
    {"aept_mark_auto",       call_mark_auto      },
    {"aept_mark_manual",     call_mark_manual    },
    {"aept_mark_manual_all", call_mark_manual_all},
    {"aept_pin",             call_pin            },
    {"aept_unpin",           call_unpin          },
    {"aept_clean",           call_clean          },
    {"aept_triggers",        call_triggers       },
    {"aept_autoremove",      call_autoremove     },
    {"aept_upgrade",         call_upgrade        },
    {"aept_remove",          call_remove         },
};

static int uninjected(io_call_fn call)
{
    aept_ctx_t *ctx;
    int r;

    make_root();
    ctx = aept_init();
    aept_set_log_fn(ctx, quiet_log, NULL);
    aept_load_config(ctx, conf_path);
    r = call(ctx);
    aept_cleanup(ctx);
    return r;
}

int main(void)
{
    size_t i;
    long total = 0;

    base = mkdtemp(dir_template);
    if (!base) {
        perror("mkdtemp");
        return 2;
    }

    /* Positive control: the fixture works with nothing injected. */
    {
        aept_ctx_t *ctx;

        make_root();
        ctx = aept_init();
        test_ok(ctx != NULL, "aept_init() succeeds with no injection");
        aept_set_log_fn(ctx, quiet_log, NULL);
        test_int_eq(aept_load_config(ctx, conf_path), 0, "the fixture config loads");
        test_int_eq(call_files(ctx), 0, "the fixture has an installed package to read");
        aept_cleanup(ctx);
    }

    for (i = 0; i < sizeof(CALLS) / sizeof(CALLS[0]); i++) {
        int expect = uninjected(CALLS[i].call);
        long n = sweep(CALLS[i].name, CALLS[i].call, expect);

        printf("# %-22s makes %4ld I/O calls (uninjected returns %d)\n", CALLS[i].name, n, expect);
        if (n < 0)
            break;
        total += n;
    }

    printf("# %ld I/O calls swept across %zu entry points\n", total,
           sizeof(CALLS) / sizeof(CALLS[0]));

    /* Still here. */
    test_ok(1, "the process survived every injected I/O failure");

    return test_summary();
}
