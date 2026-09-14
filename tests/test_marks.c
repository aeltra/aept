/* test_marks.c - the marks file behind autoremove and the protected mark
 *
 * The marks are the input that decides what autoremove may delete and
 * what nothing may, so the failures that matter are marking a package
 * the user asked for and losing a protection: every transition below
 * is checked from both sides, and a name is shown to carry one mark
 * whatever was written before.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/status.h"
#include "aept/util.h"

#include "test.h"

static struct aept_ctx ctx;
static char dir_template[] = "/tmp/aept-marks-XXXXXX";

/* Only failures to open or write the file are logged; none is
 * expected, but the context must exist before aept_log() runs. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

static int count_marked(aept_mark_t mark)
{
    aept_fileset_t set;
    int n;

    aept_fileset_init(&set);
    aept_status_load_marked(&ctx, mark, &set);
    n = set.count;
    aept_fileset_free(&set);
    return n;
}

/* Lines in the file, so duplication would show. */
static int line_count(void)
{
    FILE *fp = fopen(ctx.config.marks_file, "r");
    char buf[512];
    int n = 0;

    if (!fp)
        return 0;
    while (fgets(buf, sizeof(buf), fp))
        n++;
    fclose(fp);
    return n;
}

/* A hand edit, as far as the context is concerned: the file changes
 * behind its back, so its copy has to be dropped as a new API call
 * would. */
static void write_file(const char *text)
{
    FILE *fp = fopen(ctx.config.marks_file, "w");

    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
    aept_marks_reset(&ctx);
}

int main(void)
{
    char *dir = mkdtemp(dir_template);

    if (!dir) {
        perror("mkdtemp");
        return 2;
    }

    silence_logging();
    aept_asprintf(&ctx.config.marks_file, "%s/marks", dir);

    /* ── an absent file is all-manual, not an error ───────────────── */

    test_int_eq(aept_status_get_mark(&ctx, "foo"), AEPT_MARK_MANUAL,
                "everything is manual before any mark");
    test_int_eq(count_marked(AEPT_MARK_AUTO), 0, "the auto set loads empty from an absent file");
    test_int_eq(aept_status_set_mark(&ctx, "foo", AEPT_MARK_MANUAL), 0,
                "setting manual into an absent file is a no-op");
    test_int_eq(access(ctx.config.marks_file, F_OK), -1, "and does not create it");

    /* ── the round trip ───────────────────────────────────────────── */

    test_int_eq(aept_status_set_mark(&ctx, "foo", AEPT_MARK_AUTO), 0, "foo is marked auto");
    test_int_eq(aept_status_get_mark(&ctx, "foo"), AEPT_MARK_AUTO, "and reads back as auto");
    test_int_eq(aept_status_get_mark(&ctx, "bar"), AEPT_MARK_MANUAL, "bar does not");

    test_int_eq(aept_status_set_mark(&ctx, "foo", AEPT_MARK_AUTO), 0, "marking foo again succeeds");
    test_int_eq(line_count(), 1, "without duplicating the line");

    test_int_eq(aept_status_set_mark(&ctx, "bar", AEPT_MARK_PROTECTED), 0, "bar is protected");
    test_int_eq(aept_status_get_mark(&ctx, "bar"), AEPT_MARK_PROTECTED, "and reads back so");
    test_int_eq(count_marked(AEPT_MARK_AUTO), 1, "the auto set holds foo");
    test_int_eq(count_marked(AEPT_MARK_PROTECTED), 1, "the protected set holds bar");

    /* ── one mark per name: a change replaces, never adds ─────────── */

    test_int_eq(aept_status_set_mark(&ctx, "foo", AEPT_MARK_PROTECTED), 0, "foo goes protected");
    test_int_eq(aept_status_get_mark(&ctx, "foo"), AEPT_MARK_PROTECTED, "and reads back so");
    test_int_eq(count_marked(AEPT_MARK_AUTO), 0, "it left the auto set");
    test_int_eq(line_count(), 2, "one line per name");

    test_int_eq(aept_status_set_mark(&ctx, "foo", AEPT_MARK_AUTO), 0, "foo goes auto again");
    test_int_eq(count_marked(AEPT_MARK_PROTECTED), 1, "it left the protected set");
    test_int_eq(line_count(), 2, "still one line per name");

    /* ── manual drops the line ────────────────────────────────────── */

    test_int_eq(aept_status_set_mark(&ctx, "foo", AEPT_MARK_MANUAL), 0, "foo is made manual");
    test_int_eq(aept_status_get_mark(&ctx, "foo"), AEPT_MARK_MANUAL, "and is no longer auto");
    test_int_eq(aept_status_get_mark(&ctx, "bar"), AEPT_MARK_PROTECTED,
                "bar was not taken with it");
    test_int_eq(line_count(), 1, "the line is gone");

    test_int_eq(aept_status_set_mark(&ctx, "absent", AEPT_MARK_MANUAL), 0,
                "manual on an absent name is a no-op");
    test_int_eq(line_count(), 1, "and changes nothing");

    /* ── a name must match whole, not by prefix ───────────────────── */

    test_int_eq(aept_status_set_mark(&ctx, "barn", AEPT_MARK_AUTO), 0, "barn is marked beside bar");
    test_int_eq(aept_status_set_mark(&ctx, "bar", AEPT_MARK_MANUAL), 0, "bar is made manual");
    test_int_eq(aept_status_get_mark(&ctx, "barn"), AEPT_MARK_AUTO, "barn survives bar's change");
    test_int_eq(aept_status_get_mark(&ctx, "bar"), AEPT_MARK_MANUAL, "bar itself is gone");

    /* ── clear_auto takes the auto lines and only those ───────────── */

    test_int_eq(aept_status_set_mark(&ctx, "bar", AEPT_MARK_PROTECTED), 0,
                "bar is protected again");
    test_int_eq(aept_status_clear_auto(&ctx), 0, "the auto marks are cleared");
    test_int_eq(count_marked(AEPT_MARK_AUTO), 0, "nothing is auto after a clear");
    test_int_eq(aept_status_get_mark(&ctx, "bar"), AEPT_MARK_PROTECTED, "bar is still protected");

    /* ── damaged lines: ignored on read, dropped on write ─────────── */

    write_file("good auto\n"
               "bar\n"            /* no mark word */
               "baz auto extra\n" /* a third word */
               "qux sideways\n"   /* not a mark */
               "dup auto\n"
               "dup protected\n" /* a hand-made duplicate */
               "last protected\n");
    test_int_eq(aept_status_get_mark(&ctx, "good"), AEPT_MARK_AUTO, "a good line reads");
    test_int_eq(aept_status_get_mark(&ctx, "bar"), AEPT_MARK_MANUAL, "a bare name is manual");
    test_int_eq(aept_status_get_mark(&ctx, "baz"), AEPT_MARK_MANUAL, "a third word is ignored");
    test_int_eq(aept_status_get_mark(&ctx, "qux"), AEPT_MARK_MANUAL, "an unknown mark is ignored");
    test_int_eq(aept_status_get_mark(&ctx, "dup"), AEPT_MARK_AUTO,
                "a duplicated name reads by its first line");
    test_int_eq(aept_status_get_mark(&ctx, "last"), AEPT_MARK_PROTECTED, "the last line reads");

    test_int_eq(aept_status_set_mark(&ctx, "dup", AEPT_MARK_PROTECTED), 0, "dup is set once");
    test_int_eq(line_count(), 3, "the rewrite kept only the well-formed lines, dup once");
    test_int_eq(aept_status_get_mark(&ctx, "dup"), AEPT_MARK_PROTECTED, "and dup reads as set");

    /* ── appending to a file without its final newline ────────────── */

    write_file("last protected");
    test_int_eq(aept_status_set_mark(&ctx, "new", AEPT_MARK_AUTO), 0, "new is marked after it");
    test_int_eq(aept_status_get_mark(&ctx, "last"), AEPT_MARK_PROTECTED,
                "the unterminated last line kept its mark");
    test_int_eq(aept_status_get_mark(&ctx, "new"), AEPT_MARK_AUTO, "and the new one reads");
    test_int_eq(line_count(), 2, "on two lines");

    /* Cleanup */
    aept_marks_reset(&ctx);
    unlink(ctx.config.marks_file);
    free(ctx.config.marks_file);
    rmdir(dir);

    return test_summary();
}
