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
