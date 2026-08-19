/* test_auto_flags.c - the auto-installed set behind autoremove
 *
 * These flags are the input that decides what autoremove may delete,
 * so the failure that matters is marking a package the user asked for:
 * every transition below is checked from both sides.
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
static char dir_template[] = "/tmp/aept-auto-XXXXXX";

/* Only failures to open or write the file are logged; none is
 * expected, but the context must exist before aept_log() runs. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

static int set_count(void)
{
    aept_fileset_t set;
    int n;

    aept_fileset_init(&set);
    aept_status_load_auto_set(&ctx, &set);
    n = set.count;
    aept_fileset_free(&set);
    return n;
}

int main(void)
{
    char *dir = mkdtemp(dir_template);

    if (!dir) {
        perror("mkdtemp");
        return 2;
    }

    silence_logging();
    aept_asprintf(&ctx.config.auto_file, "%s/auto-installed", dir);

    /* ── an absent file is an empty set, not an error ─────────────── */

    test_int_eq(aept_status_is_auto(&ctx, "foo"), 0, "nothing is auto before any mark");
    test_int_eq(set_count(), 0, "the set loads empty from an absent file");
    test_int_eq(aept_status_unmark_auto(&ctx, "foo"), 0,
                "unmarking into an absent file is a no-op");

    /* ── the round trip ───────────────────────────────────────────── */

    test_int_eq(aept_status_mark_auto(&ctx, "foo"), 0, "foo is marked");
    test_int_eq(aept_status_is_auto(&ctx, "foo"), 1, "and reads back as auto");
    test_int_eq(aept_status_is_auto(&ctx, "bar"), 0, "bar does not");

    test_int_eq(aept_status_mark_auto(&ctx, "foo"), 0, "marking foo again succeeds");
    test_int_eq(set_count(), 1, "without duplicating it");

    test_int_eq(aept_status_mark_auto(&ctx, "bar"), 0, "bar is marked too");
    test_int_eq(set_count(), 2, "the set holds both");

    /* ── unmark takes exactly one name ────────────────────────────── */

    test_int_eq(aept_status_unmark_auto(&ctx, "foo"), 0, "foo is unmarked");
    test_int_eq(aept_status_is_auto(&ctx, "foo"), 0, "and is no longer auto");
    test_int_eq(aept_status_is_auto(&ctx, "bar"), 1, "bar was not taken with it");

    test_int_eq(aept_status_unmark_auto(&ctx, "absent"), 0, "unmarking an absent name is a no-op");
    test_int_eq(set_count(), 1, "and changes nothing");

    /* ── a name must match whole, not by prefix ───────────────────── */

    test_int_eq(aept_status_mark_auto(&ctx, "barn"), 0, "barn is marked beside bar");
    test_int_eq(aept_status_unmark_auto(&ctx, "bar"), 0, "bar is unmarked");
    test_int_eq(aept_status_is_auto(&ctx, "barn"), 1, "barn survives bar's unmarking");
    test_int_eq(aept_status_is_auto(&ctx, "bar"), 0, "bar itself is gone");

    /* ── clear empties the set ────────────────────────────────────── */

    test_int_eq(aept_status_mark_auto(&ctx, "bar"), 0, "bar is marked again");
    test_int_eq(aept_status_clear_auto(&ctx), 0, "the set is cleared");
    test_int_eq(set_count(), 0, "and loads empty");
    test_int_eq(aept_status_is_auto(&ctx, "bar"), 0, "nothing is auto after a clear");

    /* Cleanup */
    unlink(ctx.config.auto_file);
    free(ctx.config.auto_file);
    rmdir(dir);

    return test_summary();
}
