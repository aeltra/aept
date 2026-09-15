/* test_listfile.c - reading and writing {name}.list lines
 *
 * Every reader of a package's file list goes through listfile.c, so
 * this is where the format is pinned: the full line, the two older
 * shorter forms, what a missing column reads as, and what is dropped.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/listfile.h"
#include "aept/util.h"

#include "test.h"

static char dir_template[] = "/tmp/aept-listfile-XXXXXX";
static char *dir;
static char *path;

static void write_list(const char *text)
{
    FILE *fp = fopen(path, "w");

    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
}

int main(void)
{
    aept_list_t l;
    int i;

    dir = mkdtemp(dir_template);
    if (!dir) {
        perror("mkdtemp");
        return 2;
    }
    aept_asprintf(&path, "%s/pkg.list", dir);

    /* ── an absent list ───────────────────────────────────────────── */

    test_int_eq(aept_list_open(&l, path), -1, "an absent list does not open");
    test_int_eq(aept_list_next(&l), 0, "and yields nothing");
    aept_list_close(&l);

    /* ── the full line ────────────────────────────────────────────── */

    write_list("./usr/bin/foo\t0100755\t0\t0\t1234\t"
               "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03\n"
               "./usr/lib\t040755\t0\t0\t-\t-\n"
               "./usr/lib/x\t0120777\t0\t0\t-\t-\ty\n");
    test_int_eq(aept_list_open(&l, path), 0, "a list opens");

    test_int_eq(aept_list_next(&l), 1, "the regular file reads");
    test_str_eq(l.entry.path, "./usr/bin/foo", "with its path as written");
    test_str_eq(l.entry.stripped, "usr/bin/foo", "and stripped");
    test_int_eq((int)l.entry.mode, 0100755, "mode with type bits");
    test_int_eq((int)l.entry.uid, 0, "uid");
    test_int_eq((int)l.entry.gid, 0, "gid");
    test_int_eq((int)l.entry.size, 1234, "size");
    test_str_eq(l.entry.sha256, "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03",
                "digest");
    test_ok(l.entry.link == NULL, "no link target");
    test_ok(strncmp(l.entry.raw, "./usr/bin/foo\t", 14) == 0 &&
                l.entry.raw[strlen(l.entry.raw) - 1] == '\n',
            "the raw line is kept, newline included");

    test_int_eq(aept_list_next(&l), 1, "the directory reads");
    test_int_eq((int)l.entry.size, -1, "with no size");
    test_ok(l.entry.sha256 == NULL, "and no digest");

    test_int_eq(aept_list_next(&l), 1, "the symlink reads");
    test_str_eq(l.entry.link, "y", "with its target in the seventh column");

    test_int_eq(aept_list_next(&l), 0, "then the end");
    aept_list_close(&l);

    /* ── the shorter forms still read ─────────────────────────────── */

    write_list("./usr/bin/old\t0100755\n"
               "./usr/lib/oldlink\t0120777\tz\n"
               "/absolute\n");
    aept_list_open(&l, path);

    test_int_eq(aept_list_next(&l), 1, "a path-and-mode line reads");
    test_int_eq((int)l.entry.mode, 0100755, "with its mode");
    test_int_eq((int)l.entry.uid, -1, "uid unknown");
    test_int_eq((int)l.entry.size, -1, "size unknown");
    test_ok(l.entry.sha256 == NULL, "digest unknown");
    test_ok(l.entry.link == NULL, "no link");

    test_int_eq(aept_list_next(&l), 1, "a three-column line reads");
    test_str_eq(l.entry.link, "z", "with the third column as the link target");

    test_int_eq(aept_list_next(&l), 1, "a bare path reads");
    test_int_eq((int)l.entry.mode, 0, "with mode unknown");
    test_str_eq(l.entry.stripped, "absolute", "and a leading slash stripped");
    aept_list_close(&l);

    /* ── what is skipped ──────────────────────────────────────────── */

    {
        char *text = aept_malloc(6000);
        int n = 0;

        n += sprintf(text + n, "./before\t0100644\n\n");
        memset(text + n, 'x', 5000);
        n += 5000;
        n += sprintf(text + n, "\t0100644\n./after\t0100644\n./noeol\t0100644");
        text[n] = '\0';
        write_list(text);
        free(text);
    }
    aept_list_open(&l, path);
    test_int_eq(aept_list_next(&l), 1, "the line before reads");
    test_str_eq(l.entry.path, "./before", "as itself");
    test_int_eq(aept_list_next(&l), 1, "the blank line is skipped, the next reads");
    test_str_eq(l.entry.path, "./after", "and the over-long line was dropped whole");
    test_int_eq(aept_list_next(&l), 1, "a last line without a newline reads");
    test_str_eq(l.entry.path, "./noeol", "as itself");
    test_int_eq(aept_list_next(&l), 0, "then the end");
    aept_list_close(&l);

    /* ── the writer round-trips through the reader ────────────────── */

    {
        FILE *fp = fopen(path, "w");

        test_int_eq(aept_list_write_line(fp, "./a", 0100644, 1, 2, 42, "abc", NULL), 0,
                    "a regular file line writes");
        test_int_eq(aept_list_write_line(fp, "./d", 040755, 0, 0, -1, NULL, NULL), 0,
                    "a directory line writes");
        test_int_eq(aept_list_write_line(fp, "./l", 0120777, 0, 0, -1, NULL, "t"), 0,
                    "a symlink line writes");
        fclose(fp);
    }
    aept_list_open(&l, path);
    for (i = 0; i < 3; i++) {
        test_int_eq(aept_list_next(&l), 1, "written line reads back");
        if (i == 0) {
            test_int_eq((int)l.entry.mode, 0100644, "mode round-trips");
            test_int_eq((int)l.entry.uid, 1, "uid round-trips");
            test_int_eq((int)l.entry.gid, 2, "gid round-trips");
            test_int_eq((int)l.entry.size, 42, "size round-trips");
            test_str_eq(l.entry.sha256, "abc", "digest round-trips");
        }
        if (i == 1)
            test_ok(l.entry.size == -1 && l.entry.sha256 == NULL, "a directory has neither");
        if (i == 2)
            test_str_eq(l.entry.link, "t", "a link target round-trips");
    }
    aept_list_close(&l);

    unlink(path);
    free(path);
    rmdir(dir);

    return test_summary();
}
