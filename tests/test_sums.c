/* test_sums.c - reading a package's shipped sha256sums
 *
 * What the reader accepts is what a build tool's sha256sum(1) output
 * looks like, and nothing else: the check at install time is only as
 * good as the list, so a list that is there and wrong is refused whole
 * rather than trusted in part.
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
#include "aept/sums.h"
#include "aept/util.h"

#include "test.h"

static struct aept_ctx quiet_ctx;
static char dir_template[] = "/tmp/aept-sums-XXXXXX";
static char *path;

#define D1 "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03"
#define D2 "c9c35465c79d12978ce82af86aa8652840acdc22c8b5bcd7d828a855a55dbd57"

static void write_sums(const char *text)
{
    FILE *fp = fopen(path, "w");

    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
}

/* Load `text` and expect it refused. */
static void check_refused(const char *text, const char *label)
{
    aept_sums_t s;

    write_sums(text);
    test_int_eq(aept_sums_load(path, &s), -1, label);
    test_int_eq(s.count, 0, "and nothing is kept of it");
    aept_sums_free(&s);
}

int main(void)
{
    aept_sums_t s;
    char *dir = mkdtemp(dir_template);

    if (!dir) {
        perror("mkdtemp");
        return 2;
    }
    aept_asprintf(&path, "%s/sha256sums", dir);

    /* Every refusal logs an error; keep a passing run quiet. */
    quiet_ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&quiet_ctx);

    /* ── absent means "none shipped", not an error ────────────────── */

    test_int_eq(aept_sums_load(path, &s), 1, "an absent sha256sums reads as none");
    test_int_eq(s.count, 0, "with no entries");
    test_ok(aept_sums_lookup(&s, "usr/bin/a") == NULL, "and nothing looks up");
    aept_sums_free(&s);

    /* ── the format ───────────────────────────────────────────────── */

    write_sums(D1 "  usr/bin/a\n" D2 "  usr/share/x/data\n" D1 "  usr/bin/b\n");
    test_int_eq(aept_sums_load(path, &s), 0, "a well-formed list reads");
    test_int_eq(s.count, 3, "with every line");
    test_str_eq(aept_sums_lookup(&s, "usr/bin/a"), D1, "a path looks up its digest");
    test_str_eq(aept_sums_lookup(&s, "./usr/bin/a"), D1, "with a leading ./ ignored");
    test_str_eq(aept_sums_lookup(&s, "/usr/bin/a"), D1, "and a leading / ignored");
    test_str_eq(aept_sums_lookup(&s, "usr/bin/b"), D1,
                "a hard link's second name has its own line");
    test_ok(aept_sums_lookup(&s, "usr/bin") == NULL, "a prefix is not a match");
    test_ok(aept_sums_lookup(&s, "usr/bin/c") == NULL, "an unlisted path is NULL");
    aept_sums_free(&s);

    write_sums("5891B5B522D5DF086D0FF0B110FBD9D21BB4FC7163AF34D08286A2E846F6BE03  ./usr/bin/a\n");
    test_int_eq(aept_sums_load(path, &s), 0, "upper-case hex and a ./ prefix read");
    test_str_eq(aept_sums_lookup(&s, "usr/bin/a"), D1, "normalised to lower case, prefix stripped");
    aept_sums_free(&s);

    /* ── what is refused, whole ───────────────────────────────────── */

    check_refused("", "an empty list is refused");
    check_refused(D1 " usr/bin/a\n", "one space between digest and path is refused");
    check_refused("5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be0  usr/bin/a\n",
                  "a 63-digit digest is refused");
    check_refused("g891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  usr/bin/a\n",
                  "a non-hex digit is refused");
    check_refused(D1 "  \n", "a line naming no file is refused");
    check_refused(D1 "  usr/bin/a\n" D2 "  usr/bin/a\n", "a path listed twice is refused");
    check_refused(D1 "  usr/bin/a\nnot a sums line\n",
                  "one bad line refuses the whole list, good lines included");
    {
        char *text = aept_malloc(6000);
        int n = sprintf(text, "%s  ", D1);

        memset(text + n, 'x', 5000);
        n += 5000;
        strcpy(text + n, "\n");
        check_refused(text, "an over-long line is refused");
        free(text);
    }

    unlink(path);
    free(path);
    rmdir(dir);

    return test_summary();
}
