/* test_trigger_entries.c - parsing of {info_dir}/{pkg}.triggers
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * load_trigger_entries() is a file-scope helper in trigger.c.  Pull the
 * translation unit in directly rather than widening the internal API.
 */

#include "trigger.c"

/*
 * trigger.c now records failures through status.c, which would drag
 * the solver and libsolv into this parser-only test.  The paths that
 * call these are never reached here; the stubs only satisfy the
 * linker.
 */
int aept_status_get_state(struct aept_ctx *ctx, const char *name, char *buf, size_t buflen)
{
    (void)ctx;
    (void)name;
    (void)buf;
    (void)buflen;
    return -1;
}

int aept_status_set_state(struct aept_ctx *ctx, const char *name, const char *state)
{
    (void)ctx;
    (void)name;
    (void)state;
    return 0;
}
#include "test.h"

/*
 * A malformed .triggers file is expected below and logs a warning.
 * Route logging through a context quiet enough that the expected
 * diagnostic does not make a passing run look like a failing one.
 */
static struct aept_ctx ctx;

static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_WARNING - 1;
    aept_log_set_ctx(&ctx);
}

/* Write the given content to {info_dir}/{name}.triggers. */
static void write_triggers(const char *name, const char *content)
{
    char *path = NULL;
    FILE *fp;

    aept_asprintf(&path, "%s/%s.triggers", ctx.config.info_dir, name);

    fp = fopen(path, "w");
    if (!fp) {
        printf("Bail out! cannot write %s\n", path);
        exit(1);
    }

    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    free(path);
}

static void unlink_triggers(const char *name)
{
    char *path = NULL;

    aept_asprintf(&path, "%s/%s.triggers", ctx.config.info_dir, name);
    unlink(path);
    free(path);
}

static void free_entries(trigger_entry_t *entries, int count)
{
    for (int i = 0; i < count; i++) {
        free(entries[i].pattern);
        free(entries[i].pkg_name);
    }
    free(entries);
}

/* Load the entries currently in info_dir and check the whole set. */
static void check_entries(const char *label, int want_count, const char *want_pattern,
                          int want_modify_only)
{
    trigger_entry_t *entries = NULL;
    int count = 0;
    char buf[256];

    load_trigger_entries(&ctx, &entries, &count);

    snprintf(buf, sizeof(buf), "%s: entry count", label);
    test_int_eq(count, want_count, buf);

    if (count == want_count && want_count > 0) {
        snprintf(buf, sizeof(buf), "%s: pattern", label);
        test_str_eq(entries[0].pattern, want_pattern, buf);

        snprintf(buf, sizeof(buf), "%s: modify_only", label);
        test_int_eq(entries[0].modify_only, want_modify_only, buf);
    } else if (count != want_count) {
        for (int i = 0; i < count; i++)
            printf("#   entry %d: %.60s%s\n", i, entries[i].pattern,
                   strlen(entries[i].pattern) > 60 ? "..." : "");
    }

    free_entries(entries, count);
}

/* Build a single line of `len` filler characters ending in `tail`. */
static char *long_line(size_t len, const char *tail)
{
    size_t tail_len = strlen(tail);
    char *s = aept_malloc(len + 2);

    memset(s, 'A', len - tail_len);
    memcpy(s + len - tail_len, tail, tail_len);
    s[len] = '\n';
    s[len + 1] = '\0';

    return s;
}

int main(void)
{
    char tmpl[] = "/tmp/aept-trigger-XXXXXX";
    char *dir = mkdtemp(tmpl);

    if (!dir) {
        printf("Bail out! mkdtemp failed\n");
        return 1;
    }

    ctx.config.info_dir = dir;
    silence_logging();

    /* A well-formed file parses as expected. */
    write_triggers("pkga", "# a comment\n\n  /usr/lib/gizmo\n");
    check_entries("plain pattern", 1, "/usr/lib/gizmo", 0);

    write_triggers("pkga", "+/usr/lib/gizmo\n");
    check_entries("modify-only pattern", 1, "/usr/lib/gizmo", 1);

    /*
     * The regression.  fgets() hands an over-long line back in pieces,
     * and each piece used to be recorded as a pattern of its own: the
     * package ends up with triggers it never declared, one of which is
     * whatever the line happened to end with.  Only the declared
     * pattern on the following line may survive.
     */
    {
        char *bomb = long_line(9000, "/usr/lib/gizmo");
        char *content = NULL;

        aept_asprintf(&content, "%s/etc/declared\n", bomb);
        write_triggers("pkga", content);
        check_entries("over-long line yields no bogus patterns", 1, "/etc/declared", 0);

        free(content);
        free(bomb);
    }

    /*
     * A pattern longer than the old 1024-byte buffer but well inside
     * PATH_MAX is legitimate and must still be read whole.
     */
    {
        char *line = long_line(1500, "/gizmo");
        char *want = aept_strdup(line);

        want[strlen(want) - 1] = '\0'; /* drop the newline */
        write_triggers("pkga", line);
        check_entries("a 1500-byte pattern is read whole", 1, want, 0);

        free(want);
        free(line);
    }

    unlink_triggers("pkga");
    rmdir(dir);

    return test_summary();
}
