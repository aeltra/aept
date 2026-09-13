/* test_stanza.c - reading a field back out of a control stanza
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * The input is attacker-chosen: an index arrives from a mirror and a
 * .control comes out of a package.  What matters is that a field is
 * answered from the stanza asked for and no other -- an index holds
 * many versions of a package and many packages -- and that a line the
 * reader cannot hold is dropped whole rather than read in pieces.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/stanza.h"

#include "test.h"

static char path[] = "/tmp/aept-stanza-XXXXXX";

static void write_raw(const char *text)
{
    FILE *fp = fopen(path, "w");

    if (!fp) {
        perror(path);
        exit(1);
    }
    fputs(text, fp);
    fclose(fp);
}

/* Look up one field of one stanza, as a string test can compare. */
static char *field_of(const char *name, const char *version, const char *field)
{
    char *stanza = aept_stanza_find(path, name, version);
    char *v = stanza ? aept_stanza_field(stanza, field) : NULL;

    free(stanza);
    return v;
}

/* Record how many stanzas a walk saw, and the names at each end. */
struct stanza_seen {
    int n;
    char first[64];
    char last[64];
};

static int count_stanza(const char *stanza, void *user)
{
    struct stanza_seen *seen = user;
    char *name = aept_stanza_field(stanza, "Package");

    if (name) {
        if (!seen->n)
            snprintf(seen->first, sizeof(seen->first), "%s", name);
        snprintf(seen->last, sizeof(seen->last), "%s", name);
        free(name);
    }
    seen->n++;
    return 0;
}

/* Every stanza's Depends must hold exactly the entries it was built
 * with -- the check that a block boundary did not cut one short. */
static int check_depends(const char *stanza, void *user)
{
    struct stanza_seen *seen = user;
    char *name = aept_stanza_field(stanza, "Package");
    char *dep = aept_stanza_field(stanza, "Depends");
    int i = name ? atoi(name + 3) : -1;
    int want = (i % 97) + 1, got = 0;
    const char *p = dep;
    int bad = 0;

    while (p && *p) {
        got++;
        p = strchr(p, ',');
        if (p)
            p++;
    }
    if (i < 0 || got != want)
        bad = 1;

    seen->n++;
    free(name);
    free(dep);
    return bad;
}

static int stop_at_first(const char *stanza, void *user)
{
    count_stanza(stanza, user);
    return 1;
}

#define INDEX                                                                                      \
    "Package: alpha\n"                                                                             \
    "Version: 1.0\n"                                                                               \
    "Depends: one\n"                                                                               \
    "\n"                                                                                           \
    "Package: beta\n"                                                                              \
    "Version: 2.0\n"                                                                               \
    "Depends: two (>= 1), three\n"                                                                 \
    "Description: b\n"                                                                             \
    "\n"                                                                                           \
    "Package: alpha\n"                                                                             \
    "Version: 3.0\n"                                                                               \
    "Depends: three\n"                                                                             \
    "\n"

int main(void)
{
    char *v;
    int fd;

    fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    close(fd);

    /* ── the right stanza out of several ───────────────────────────── */

    write_raw(INDEX);

    v = field_of("alpha", "1.0", "Depends");
    test_str_eq(v, "one", "the first alpha is found by its version");
    free(v);

    v = field_of("alpha", "3.0", "Depends");
    test_str_eq(v, "three", "and the second, which shares its name");
    free(v);

    v = field_of("beta", "2.0", "Depends");
    test_str_eq(v, "two (>= 1), three", "a value is returned verbatim, parentheses and all");
    free(v);

    v = field_of("alpha", "2.0", "Depends");
    test_ok(v == NULL, "a version that no stanza carries is not found");
    free(v);

    v = field_of("gamma", "1.0", "Depends");
    test_ok(v == NULL, "a package that is not there is not found");
    free(v);

    v = field_of("beta", "2.0", "Recommends");
    test_ok(v == NULL, "a field the stanza does not carry reads as absent");
    free(v);

    /* ── how a field is matched ────────────────────────────────────── */

    write_raw("Package: p\nVersion: 1\ndepends: lower\nPre-Depends: pre\n\n");

    v = field_of("p", "1", "Depends");
    test_str_eq(v, "lower", "a field name matches without regard to case");
    free(v);

    /* "Depends" must not match inside "Pre-Depends": the prefix check
     * has to start at the beginning of a line. */
    write_raw("Package: p\nVersion: 1\nPre-Depends: pre\n\n");
    v = field_of("p", "1", "Depends");
    test_ok(v == NULL, "Pre-Depends is not mistaken for Depends");
    free(v);

    /* ── continuation lines ────────────────────────────────────────── */

    write_raw("Package: p\nVersion: 1\nDepends: one,\n two,\n three\nSuggests: s\n\n");

    v = field_of("p", "1", "Depends");
    test_str_eq(v, "one, two, three", "continuation lines are folded onto one");
    free(v);

    v = field_of("p", "1", "Suggests");
    test_str_eq(v, "s", "and the field after them is still found");
    free(v);

    /* ── whitespace ────────────────────────────────────────────────── */

    write_raw("Package: p\nVersion: 1\nDepends:   spaced   \n\n");
    v = field_of("p", "1", "Depends");
    test_str_eq(v, "spaced", "the space either side of a value is trimmed");
    free(v);

    /* ── shapes that are not a well-formed index ───────────────────── */

    /* A file of one stanza need not end with a blank line, and a
     * .control does not. */
    write_raw("Package: p\nVersion: 1\nDepends: d\n");
    v = field_of("p", "1", "Depends");
    test_str_eq(v, "d", "a stanza ending at end-of-file is still read");
    free(v);

    /* An over-long line is dropped whole: reading its tail as a line of
     * its own would invent a field out of somebody's dependency list. */
    {
        FILE *fp = fopen(path, "w");
        int i;

        fputs("Package: p\nVersion: 1\nDepends: ", fp);
        for (i = 0; i < 9000; i++)
            fputc('x', fp);
        fputs("\nSuggests: after\n\n", fp);
        fclose(fp);

        v = field_of("p", "1", "Depends");
        test_ok(v == NULL, "an over-long field is dropped, not truncated");
        free(v);

        v = field_of("p", "1", "Suggests");
        test_str_eq(v, "after", "and the line after it is read normally");
        free(v);
    }

    /*
     * Folding versus keeping the lines.  A relationship field is one
     * logical value however it was wrapped, so continuations join with
     * a single space whatever the indent.  A Description is laid out by
     * its author: its continuations keep their newline and lose only
     * the one character that marked them, so any further indent is the
     * author's and survives.
     */
    write_raw("Package: p\n"
              "Version: 1\n"
              "Depends: one,\n"
              "   two,\n"
              "\tthree\n"
              "Description: summary\n"
              " body\n"
              "   indented\n"
              "\n");
    {
        char *stanza = aept_stanza_find(path, "p", "1");

        v = aept_stanza_field(stanza, "Depends");
        test_str_eq(v, "one, two, three", "a folded field joins with one space per line");
        free(v);

        v = aept_stanza_field_lines(stanza, "Depends");
        test_str_eq(v, "one,\n  two,\nthree", "kept apart, only the marking character goes");
        free(v);

        v = aept_stanza_field_lines(stanza, "Description");
        test_str_eq(v, "summary\nbody\n  indented",
                    "a Description keeps its lines and the author's indent");
        free(v);

        v = aept_stanza_field_lines(stanza, "Absent");
        test_ok(v == NULL, "a field that is not there reads as nothing either way");
        free(v);

        free(stanza);
    }

    /*
     * The field iterator, which is what deb.c reads an index with.
     * Its input is attacker-chosen, so what it does with lines that are
     * not fields matters as much as what it does with lines that are.
     */
    {
        const char *pos;
        aept_stanza_field_t f;
        char *val;

        /* A stanza opening with a continuation belonging to no field,
         * and with blank lines: both are skipped to reach the field. */
        pos = "  orphan continuation\n\n\nReal: value\n";
        test_ok(aept_stanza_next_field(&pos, &f), "an orphan continuation does not end the walk");
        test_ok(aept_stanza_field_is(&f, "Real"), "the field after it is the one returned");
        val = aept_stanza_value(&f, 0);
        test_str_eq(val, "value", "and its value is intact");
        free(val);
        test_ok(!aept_stanza_next_field(&pos, &f), "then the stanza is exhausted");

        /* A trailing line that is not a field, with no newline after
         * it: the walk ends rather than inventing a field from it. */
        pos = "Name: value\nno colon and no newline";
        test_ok(aept_stanza_next_field(&pos, &f), "the field before the junk is read");
        test_ok(aept_stanza_field_is(&f, "Name"), "and it is the right one");
        test_ok(!aept_stanza_next_field(&pos, &f), "a trailing non-field ends the walk");

        /* Matching is by whole name, not by prefix. */
        pos = "Name: value\n";
        test_ok(aept_stanza_next_field(&pos, &f), "a field is read");
        test_ok(!aept_stanza_field_is(&f, "Nam"), "a shorter name does not match");
        test_ok(!aept_stanza_field_is(&f, "Names"), "nor a longer one");
        test_ok(aept_stanza_field_is(&f, "nAmE"), "case does not matter");
    }

    /*
     * A Description whose first line is empty: the synopsis is empty
     * and the body is everything else.  Keeping the lines apart has to
     * preserve that empty first line, or deb.c -- which splits summary
     * from body at the first newline -- would take the first body line
     * as the synopsis.  Folded, there is no such line to preserve and
     * the leading separator goes.
     */
    write_raw("Package: p\nVersion: 1\nDescription:\n body\n more\n\n");
    {
        char *stanza = aept_stanza_find(path, "p", "1");

        v = aept_stanza_field_lines(stanza, "Description");
        test_str_eq(v, "\nbody\nmore", "an empty synopsis stays an empty first line");
        free(v);

        v = aept_stanza_field(stanza, "Description");
        test_str_eq(v, "body more", "folded, it starts at the first word");
        free(v);

        free(stanza);
    }

    /* A NULL version takes whichever version comes first. */
    write_raw(INDEX);
    {
        char *stanza = aept_stanza_find(path, "alpha", NULL);

        v = stanza ? aept_stanza_field(stanza, "Version") : NULL;
        test_str_eq(v, "1.0", "a NULL version matches the first stanza for the name");
        free(v);
        free(stanza);
    }

    /*
     * Splitting a file into stanzas, which is how deb.c reads an index.
     * A blank line separates them; leading ones belong to nothing, and
     * a file need not end with one.
     */
    write_raw("\n\nPackage: one\nVersion: 1\n\n\nPackage: two\nVersion: 2\n");
    {
        FILE *fp = fopen(path, "r");
        struct stanza_seen seen = {0, "", ""};

        test_int_eq(aept_stanza_foreach(fp, count_stanza, &seen), 0, "the walk reaches the end");
        test_int_eq(seen.n, 2, "leading and doubled blank lines make no extra stanzas");
        test_str_eq(seen.first, "one", "the first stanza is the first one with content");
        test_str_eq(seen.last, "two", "and the last needs no blank line after it");
        fclose(fp);
    }

    /* A callback that stops asks for the walk to end there. */
    {
        FILE *fp = fopen(path, "r");
        struct stanza_seen seen = {0, "", ""};

        test_int_eq(aept_stanza_foreach(fp, stop_at_first, &seen), 1,
                    "the callback's value is what is returned");
        test_int_eq(seen.n, 1, "and nothing is read past it");
        fclose(fp);
    }

    /* Trailing whitespace with no newline after it ends the walk
     * rather than being read as a field. */
    {
        const char *pos = "Name: value\n   ";
        aept_stanza_field_t f;

        test_ok(aept_stanza_next_field(&pos, &f), "the field before the trailing space is read");
        test_ok(!aept_stanza_next_field(&pos, &f), "unterminated trailing space ends the walk");
    }

    /*
     * An index bigger than the reader's block, so stanzas and the
     * lines inside them straddle the boundary between two reads.  The
     * splitter hands out pointers into that buffer, so a stanza that
     * is only half-there when a block runs out has to be held back
     * until the rest arrives -- get that wrong and fields are silently
     * truncated or invented at every 64 KB.
     *
     * Field lengths vary deliberately, so the boundary lands in a
     * different place in each stanza rather than always between two.
     */
    {
        FILE *fp = fopen(path, "w");
        struct stanza_seen seen = {0, "", ""};
        int i, j, want = 700;

        if (!fp) {
            perror(path);
            exit(1);
        }
        for (i = 0; i < want; i++) {
            fprintf(fp, "Package: pkg%d\nVersion: %d.0\nDepends: ", i, i);
            for (j = 0; j <= i % 97; j++)
                fprintf(fp, "%sdep%d_%d", j ? ", " : "", i, j);
            fprintf(fp, "\nDescription: package %d\n\n", i);
        }
        fclose(fp);

        fp = fopen(path, "r");
        aept_stanza_foreach(fp, count_stanza, &seen);
        fclose(fp);

        test_int_eq(seen.n, want, "every stanza of a multi-block index is found");
        test_str_eq(seen.first, "pkg0", "the first is intact");
        test_str_eq(seen.last, "pkg699", "and so is the last");
    }

    /* The same, checking a field rather than just the count: a stanza
     * cut by a block boundary must still read back whole. */
    {
        FILE *fp = fopen(path, "r");
        struct stanza_seen seen = {0, "", ""};

        test_int_eq(aept_stanza_foreach(fp, check_depends, &seen), 0,
                    "and every Depends across the boundary reads back whole");
        fclose(fp);
    }

    /* An over-long line in the last stanza, which ends at end of file
     * rather than at a blank line. */
    write_raw("Package: p\nVersion: 1\n");
    {
        FILE *fp = fopen(path, "a");
        int i;

        for (i = 0; i < 9000; i++)
            fputc(i ? 'x' : 'D', fp);
        fputs("\nSuggests: last\n", fp);
        fclose(fp);

        fp = fopen(path, "r");
        {
            struct stanza_seen seen = {0, "", ""};
            aept_stanza_foreach(fp, count_stanza, &seen);
            test_int_eq(seen.n, 1, "a final stanza with an over-long line is still one stanza");
        }
        fclose(fp);

        v = field_of("p", "1", "Suggests");
        test_str_eq(v, "last", "and the field after the over-long line survives");
        free(v);
    }

    /* A stanza with no Package line belongs to nobody. */
    write_raw("Version: 1\nDepends: d\n\n");
    v = field_of("p", "1", "Depends");
    test_ok(v == NULL, "a stanza with no Package line matches nothing");
    free(v);

    test_ok(aept_stanza_find("/nonexistent/aept/index", "p", "1") == NULL,
            "a file that is not there reads as no stanza");

    unlink(path);
    return test_summary();
}
