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

/* A join that must succeed, checked against the path it should produce. */
static void check_join(const char *prefix, const char *entry, const char *want, const char *label)
{
    char *got = NULL;
    int r = safe_join(prefix, entry, &got);
    test_int_eq(r, JOIN_OK, label);
    test_str_eq(got, want, label);
    free(got);
}

/* A join that must not succeed, checked against *which* refusal it is. */
static void check_join_fails(const char *prefix, const char *entry, int want, const char *label)
{
    char sentinel[] = "not-cleared";
    char *got = sentinel;
    test_int_eq(safe_join(prefix, entry, &got), want, label);
    test_str_eq(got, NULL, label);
}

/*
 * Drive the entry rewriter itself, which is where the distinction has
 * to survive to be worth anything: an escaping path must reach the
 * caller as JOIN_ESCAPE (fatal) and not as JOIN_SKIP (continue with the
 * next entry).  A hardlink target is the case that matters most, since
 * it is the one path in an archive that aept_archive_path_is_safe()
 * never sees.
 */
static void check_rewrite(const char *path, const char *hardlink, int want, const char *label)
{
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, path);
    if (hardlink)
        archive_entry_set_hardlink(e, hardlink);
    test_int_eq(rewrite_all_paths(e, "/opt/root"), want, label);
    archive_entry_free(e);
}

static void check_normalize(const char *raw, const char *want, const char *label)
{
    char *got = normalize_path(raw);
    test_str_eq(got, want, label);
    free(got);
}

int main(void)
{
    silence_logging();

    /* ── normalize_path ──────────────────────────────────────────── */

    check_normalize("/", "/", "normalize: root");
    check_normalize("//", "/", "normalize: doubled slash");
    check_normalize("", "", "normalize: empty");
    check_normalize("./a/./b", "a/b", "normalize: dot components");
    check_normalize("a/../b", "b", "normalize: parent component");
    check_normalize("/a/../../b", "/b", "normalize: parent clamped at root");
    check_normalize("a//b///c", "a/b/c", "normalize: repeated slashes");
    check_normalize("/usr/bin/", "/usr/bin", "normalize: trailing slash");

    /*
     * ── safe_join with a root prefix ─────────────────────────────
     *
     * aept_config_root_path(cfg, "/") yields exactly "/" when no offline
     * root is configured, which is the normal on-device case.  Every
     * entry must join cleanly; a regression here silently extracts
     * nothing while still recording the package as installed.
     */
    check_join("/", "usr/bin/foo", "/usr/bin/foo", "join: root prefix, relative entry");
    check_join("/", "./etc/passwd", "/etc/passwd", "join: root prefix, leading ./");
    check_join("/", "/etc/passwd", "/etc/passwd", "join: root prefix, absolute entry");
    check_join("/", "a//b", "/a/b", "join: root prefix, repeated slashes");
    check_join("", "usr/bin/foo", "/usr/bin/foo", "join: empty prefix behaves like root");

    /* ── safe_join with a real prefix ────────────────────────────── */

    check_join("/opt/root", "usr/bin/foo", "/opt/root/usr/bin/foo", "join: offline root");
    check_join("/opt/root/", "usr/bin/foo", "/opt/root/usr/bin/foo",
               "join: offline root, trailing slash");
    check_join("/tmp/x", "control", "/tmp/x/control", "join: control archive into tmpdir");
    check_join(NULL, "./control", "control", "join: NULL prefix strips leading ./");

    /* ── entries that must be skipped ────────────────────────────── */

    check_join_fails("/", ".", JOIN_SKIP, "join: bare dot is skipped");
    check_join_fails("/", "", JOIN_SKIP, "join: empty entry is skipped");
    check_join_fails("/opt/root", "./", JOIN_SKIP, "join: dot-slash only is skipped");

    /* ── containment must still hold for non-root prefixes ─────────
     *
     * JOIN_ESCAPE, not JOIN_SKIP: these are not entries to pass over,
     * they are entries no caller may continue past.
     */

    check_join_fails("/opt/root", "../etc/passwd", JOIN_ESCAPE,
                     "join: single parent escape rejected");
    check_join_fails("/opt/root", "a/../../../../etc/passwd", JOIN_ESCAPE,
                     "join: repeated parent escape rejected");
    check_join_fails("/opt/root", "/../etc/passwd", JOIN_ESCAPE,
                     "join: absolute parent escape rejected");

    /* ── the distinction reaches the caller ──────────────────────── */

    check_rewrite("usr/bin/foo", NULL, JOIN_OK, "rewrite: ordinary entry");
    check_rewrite("usr/bin/foo", "usr/bin/bar", JOIN_OK, "rewrite: contained hardlink");
    check_rewrite(".", NULL, JOIN_SKIP, "rewrite: bare dot skipped");
    check_rewrite("usr/bin/foo", ".", JOIN_SKIP, "rewrite: hardlink to nowhere skipped");
    check_rewrite("../etc/passwd", NULL, JOIN_ESCAPE, "rewrite: escaping pathname is fatal");
    check_rewrite("usr/bin/foo", "../../../etc/passwd", JOIN_ESCAPE,
                  "rewrite: escaping hardlink target is fatal");

    /*
     * Under a root prefix there is nothing to escape to: normalize_path()
     * clamps ".." at the root, so the result stays absolute and inside.
     * Such entries are rejected earlier anyway by
     * aept_archive_path_is_safe(), which is asserted below.
     */
    check_join("/", "../../etc/passwd", "/etc/passwd", "join: parent clamped at root prefix");

    /* ── aept_archive_path_is_safe ───────────────────────────────── */

    test_int_eq(aept_archive_path_is_safe("usr/bin/foo"), 1, "is_safe: ordinary path");
    test_int_eq(aept_archive_path_is_safe("usr/../foo"), 0, "is_safe: parent component rejected");
    test_int_eq(aept_archive_path_is_safe("../etc/passwd"), 0, "is_safe: leading parent rejected");
    test_int_eq(aept_archive_path_is_safe("a\nb"), 0,
                "is_safe: newline rejected (.list line injection)");
    test_int_eq(aept_archive_path_is_safe("a\tb"), 0,
                "is_safe: tab rejected (.list field injection)");
    test_int_eq(aept_archive_path_is_safe(""), 0, "is_safe: empty path rejected");
    test_int_eq(aept_archive_path_is_safe(NULL), 0, "is_safe: NULL rejected");
    /* Stricter than strictly necessary, but documented: any two adjacent
     * dots are refused, not just a whole ".." component. */
    test_int_eq(aept_archive_path_is_safe("lib/foo..bar"), 0,
                "is_safe: adjacent dots anywhere rejected");

    return test_summary();
}
