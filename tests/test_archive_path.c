/* test_archive_path.c - archive path normalization and containment
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdlib.h>

/*
 * normalize_path() and safe_join() are file-scope helpers in archive.c.
 * Pull the translation unit in directly so they can be exercised without
 * widening the internal API surface.
 */
#include "archive.c"

#include "aept/internal.h"

#include "test.h"

/*
 * Several cases below assert that a path is rejected, and safe_join()
 * logs an error when it rejects one.  Route logging through a context
 * whose verbosity is below AEPT_LOG_ERROR so those expected diagnostics
 * do not make a passing run look like a failing one.
 */
static struct aept_ctx quiet_ctx;

static void silence_logging(void)
{
    quiet_ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&quiet_ctx);
}

static void check_join(const char *prefix, const char *entry,
                       const char *want, const char *label)
{
    char *got = safe_join(prefix, entry);
    test_str_eq(got, want, label);
    free(got);
}

static void check_normalize(const char *raw, const char *want,
                            const char *label)
{
    char *got = normalize_path(raw);
    test_str_eq(got, want, label);
    free(got);
}

int main(void)
{
    silence_logging();

    /* ── normalize_path ──────────────────────────────────────────── */

    check_normalize("/",            "/",        "normalize: root");
    check_normalize("//",           "/",        "normalize: doubled slash");
    check_normalize("",             "",         "normalize: empty");
    check_normalize("./a/./b",      "a/b",      "normalize: dot components");
    check_normalize("a/../b",       "b",        "normalize: parent component");
    check_normalize("/a/../../b",   "/b",       "normalize: parent clamped at root");
    check_normalize("a//b///c",     "a/b/c",    "normalize: repeated slashes");
    check_normalize("/usr/bin/",    "/usr/bin", "normalize: trailing slash");

    /*
     * ── safe_join with a root prefix ─────────────────────────────
     *
     * aept_config_root_path(cfg, "/") yields exactly "/" when no offline
     * root is configured, which is the normal on-device case.  Every
     * entry must join cleanly; a regression here silently extracts
     * nothing while still recording the package as installed.
     */
    check_join("/", "usr/bin/foo",   "/usr/bin/foo",
               "join: root prefix, relative entry");
    check_join("/", "./etc/passwd",  "/etc/passwd",
               "join: root prefix, leading ./");
    check_join("/", "/etc/passwd",   "/etc/passwd",
               "join: root prefix, absolute entry");
    check_join("/", "a//b",          "/a/b",
               "join: root prefix, repeated slashes");
    check_join("",  "usr/bin/foo",   "/usr/bin/foo",
               "join: empty prefix behaves like root");

    /* ── safe_join with a real prefix ────────────────────────────── */

    check_join("/opt/root",  "usr/bin/foo", "/opt/root/usr/bin/foo",
               "join: offline root");
    check_join("/opt/root/", "usr/bin/foo", "/opt/root/usr/bin/foo",
               "join: offline root, trailing slash");
    check_join("/tmp/x",     "control",     "/tmp/x/control",
               "join: control archive into tmpdir");
    check_join(NULL,         "./control",   "control",
               "join: NULL prefix strips leading ./");

    /* ── entries that must be skipped ────────────────────────────── */

    check_join("/",         ".",  NULL, "join: bare dot is skipped");
    check_join("/",         "",   NULL, "join: empty entry is skipped");
    check_join("/opt/root", "./", NULL, "join: dot-slash only is skipped");

    /* ── containment must still hold for non-root prefixes ───────── */

    check_join("/opt/root", "../etc/passwd", NULL,
               "join: single parent escape rejected");
    check_join("/opt/root", "a/../../../../etc/passwd", NULL,
               "join: repeated parent escape rejected");
    check_join("/opt/root", "/../etc/passwd", NULL,
               "join: absolute parent escape rejected");

    /*
     * Under a root prefix there is nothing to escape to: normalize_path()
     * clamps ".." at the root, so the result stays absolute and inside.
     * Such entries are rejected earlier anyway by
     * aept_archive_path_is_safe(), which is asserted below.
     */
    check_join("/", "../../etc/passwd", "/etc/passwd",
               "join: parent clamped at root prefix");

    /* ── aept_archive_path_is_safe ───────────────────────────────── */

    test_int_eq(aept_archive_path_is_safe("usr/bin/foo"), 1,
                "is_safe: ordinary path");
    test_int_eq(aept_archive_path_is_safe("usr/../foo"), 0,
                "is_safe: parent component rejected");
    test_int_eq(aept_archive_path_is_safe("../etc/passwd"), 0,
                "is_safe: leading parent rejected");
    test_int_eq(aept_archive_path_is_safe("a\nb"), 0,
                "is_safe: newline rejected (.list line injection)");
    test_int_eq(aept_archive_path_is_safe("a\tb"), 0,
                "is_safe: tab rejected (.list field injection)");
    test_int_eq(aept_archive_path_is_safe(""), 0,
                "is_safe: empty path rejected");
    test_int_eq(aept_archive_path_is_safe(NULL), 0,
                "is_safe: NULL rejected");
    /* Stricter than strictly necessary, but documented: any two adjacent
     * dots are refused, not just a whole ".." component. */
    test_int_eq(aept_archive_path_is_safe("lib/foo..bar"), 0,
                "is_safe: adjacent dots anywhere rejected");

    return test_summary();
}
