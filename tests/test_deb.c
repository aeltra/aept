/* test_deb.c - control stanzas into libsolv solvables
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * The input is attacker-chosen: an index arrives from a mirror and a
 * .control comes out of a package.  Two properties matter here.
 *
 * A dependency that parses into something weaker than it says is the
 * dangerous failure -- a relation silently dropped installs a package
 * against a version it was never built for, and nothing downstream can
 * notice.  So the parser refuses what it cannot represent, and these
 * pin the refusals.
 *
 * And Replaces must not reach the solver as an obsoletes, or a package
 * that merely conflicts becomes an upgrade candidate for its rival.
 * See deb.h.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/repo.h>
#include <solv/solvable.h>

#include "aept/deb.h"

#include "test.h"

static Pool *pool;
static Repo *repo;

/* Parse one stanza, returning its solvable or NULL if it was refused. */
static Solvable *parse(const char *body)
{
    char *text;
    Id p;

    if (asprintf(&text, "Package: p\nVersion: 1\nArchitecture: all\n%s", body) < 0)
        exit(1);

    p = aept_deb_add_control(repo, text);
    free(text);

    return p ? pool_id2solvable(pool, p) : NULL;
}

/* The dep array under key, rendered as "a, b, c" for comparison. */
static char *deps_of(Solvable *s, Id key)
{
    Queue q;
    char *out = NULL;
    size_t len = 0;
    FILE *fp = open_memstream(&out, &len);
    int i;

    queue_init(&q);
    solvable_lookup_deparray(s, key, &q, 0);
    for (i = 0; i < q.count; i++)
        fprintf(fp, "%s%s", i ? ", " : "", pool_dep2str(pool, q.elements[i]));
    fclose(fp);
    queue_free(&q);

    return out;
}

static void check_dep(const char *field, const char *want, const char *label)
{
    char *body, *got;
    Solvable *s;

    if (asprintf(&body, "Depends: %s\n", field) < 0)
        exit(1);
    s = parse(body);
    free(body);

    if (!s) {
        test_ok(want == NULL, label);
        return;
    }
    if (!want) {
        test_ok(0, label);
        return;
    }

    got = deps_of(s, SOLVABLE_REQUIRES);
    test_str_eq(got, want, label);
    free(got);
}

int main(void)
{
    Solvable *s;
    char *got;

    pool = pool_create();
    repo = repo_create(pool, "test");

    /* ── the grammar ─────────────────────────────────────────────── */

    check_dep("libc6", "libc6", "a bare name");
    check_dep("libc6 (>= 2.34)", "libc6 >= 2.34", "a versioned relation");
    check_dep("libc6(>=2.34)", "libc6 >= 2.34", "no space anywhere");
    check_dep("a, b, c", "a, b, c", "several entries");
    check_dep("a (= 1), b (<< 2)", "a = 1, b << 2", "several versioned entries");
    check_dep("libssl3 | libssl1.1", "libssl3 | libssl1.1", "an alternative");
    check_dep("a | b | c", "a | b | c", "a chain of alternatives");
    check_dep("  a  ,  b  ", "a, b", "space around entries is not part of them");
    check_dep("a,,b", "a, b", "an empty entry is skipped, not refused");
    check_dep("", "", "an empty field is not a refusal");

    /* Policy 7.1: "<" and ">" alone are the deprecated spellings of
     * "<=" and ">=", not the strict relations. */
    check_dep("a (< 2)", "a <= 2", "a bare < means <=");
    check_dep("a (> 2)", "a >= 2", "a bare > means >=");
    check_dep("a (<= 2)", "a <= 2", "<= is itself");
    check_dep("a (>> 2)", "a >> 2", ">> is strict");

    /* ── what is refused ─────────────────────────────────────────── */

    check_dep("a (>= 1", NULL, "an unterminated relation is refused");
    check_dep("a (>= )", NULL, "a relation with no version is refused");
    check_dep("a (1.0)", NULL, "a version with no operator is refused");
    check_dep("a (>= 1) junk", NULL, "trailing text is refused");
    check_dep("a b", NULL, "two names in one entry are refused");
    check_dep("a |", NULL, "an alternative with nothing after it is refused");
    check_dep("(>= 1)", NULL, "a relation with no name is refused");

    /* A refusal drops the package rather than the field: a solvable
     * carrying only the dependencies that happened to parse is the
     * failure this is here to prevent. */
    test_ok(parse("Depends: good, a (>= 1\n") == NULL,
            "one bad entry drops the package, not just the entry");

    /* ── Replaces is not obsoletes ───────────────────────────────── */

    s = parse("Conflicts: exim\nReplaces: exim\n");
    test_ok(s != NULL, "Conflicts plus Replaces parses");

    got = deps_of(s, SOLVABLE_OBSOLETES);
    test_str_eq(got, "", "Conflicts plus Replaces derives no obsoletes");
    free(got);

    got = deps_of(s, aept_deb_replaces_key(pool));
    test_str_eq(got, "exim", "Replaces is kept under aept's own key");
    free(got);

    got = deps_of(s, SOLVABLE_CONFLICTS);
    test_str_eq(got, "exim", "Conflicts reaches the solver as a conflict");
    free(got);

    test_ok(aept_deb_takes_over(pool, s, pool_str2id(pool, "exim", 0)), "the pair is a takeover");

    s = parse("Replaces: exim\n");
    test_ok(!aept_deb_takes_over(pool, s, pool_str2id(pool, "exim", 0)),
            "Replaces alone is not a takeover");

    s = parse("Conflicts: exim\n");
    test_ok(!aept_deb_takes_over(pool, s, pool_str2id(pool, "exim", 0)),
            "Conflicts alone is not a takeover");

    /* Disjoint lists: chrony conflicts with ntp and replaces its own
     * renamed predecessor.  Neither statement is about the other. */
    s = parse("Conflicts: ntp\nReplaces: chrony-legacy\n");
    test_ok(!aept_deb_takes_over(pool, s, pool_str2id(pool, "ntp", 0)),
            "a conflict that is not also replaced is not a takeover");
    test_ok(!aept_deb_takes_over(pool, s, pool_str2id(pool, "chrony-legacy", 0)),
            "a replace that is not also conflicted is not a takeover");

    /* Partial overlap: the takeover is the intersection, by name. */
    s = parse("Conflicts: libfoo, libbar\nReplaces: libbar\n");
    test_ok(aept_deb_takes_over(pool, s, pool_str2id(pool, "libbar", 0)),
            "the overlap of the two lists is a takeover");
    test_ok(!aept_deb_takes_over(pool, s, pool_str2id(pool, "libfoo", 0)),
            "the rest of the conflicts is not");

    /* A versioned Replaces still names the package. */
    s = parse("Conflicts: exim (<< 2)\nReplaces: exim (<< 2)\n");
    test_ok(aept_deb_takes_over(pool, s, pool_str2id(pool, "exim", 0)),
            "a versioned pair is matched by name");

    /* ── the rest of the stanza ──────────────────────────────────── */

    s = parse("Depends: libc6\n");
    got = deps_of(s, SOLVABLE_PROVIDES);
    test_str_eq(got, "p = 1", "a package provides itself at its own version");
    free(got);

    s = parse("Provides: mail-transport-agent\n");
    got = deps_of(s, SOLVABLE_PROVIDES);
    test_str_eq(got, "mail-transport-agent, p = 1", "a declared provide keeps the implicit one");
    free(got);

    /* Breaks is a conflict to the solver, and lands in the same array. */
    s = parse("Conflicts: a\nBreaks: b\n");
    got = deps_of(s, SOLVABLE_CONFLICTS);
    test_str_eq(got, "a, b", "Breaks joins Conflicts");
    free(got);

    s = parse("Installed-Size: 72\n");
    test_ok(solvable_lookup_num(s, SOLVABLE_INSTALLSIZE, 0) == 72 * 1024,
            "Installed-Size is kB in the stanza and bytes in the pool");

    s = parse("Size: 341850\n");
    test_ok(solvable_lookup_num(s, SOLVABLE_DOWNLOADSIZE, 0) == 341850,
            "Size is the download size, in bytes");

    s = parse("Description: a summary\n a first body line\n a second\n");
    test_str_eq(solvable_lookup_str(s, SOLVABLE_SUMMARY), "a summary",
                "the summary is the first line of Description");
    test_str_eq(solvable_lookup_str(s, SOLVABLE_DESCRIPTION), "a first body line\na second",
                "the body keeps its lines apart");

    s = parse("Description: only one line\n");
    test_str_eq(solvable_lookup_str(s, SOLVABLE_DESCRIPTION), "only one line",
                "a one-line Description is both summary and body");

    /* ── a whole index ──────────────────────────────────────────── */
    /*
     * aept_deb_add_control takes one stanza; an index is many, and the
     * splitting is its own job.  The index is attacker-chosen, so a
     * line too long to hold must be dropped whole -- reading its tail
     * as a line of its own would invent a field out of the middle of
     * somebody's Description.
     */
    {
        Repo *ir = repo_create(pool, "index");
        char *text;
        size_t len;
        FILE *fp = open_memstream(&text, &len);
        int i, n = 0;
        Id ip;

        /* The header stanza an index opens with, then two packages. */
        fputs("Origin: Aeltra\nValid-Until: 2026-09-10T06:15:55Z\n\n", fp);
        fputs("Package: alpha\nVersion: 1\nArchitecture: all\nDepends: libc6\n\n", fp);
        fputs("Package: gamma\nVersion: 3\nArchitecture: all\nDescription: g\n", fp);
        /* No trailing blank line: a stanza may end at end of file. */
        fclose(fp);

        fp = fmemopen(text, len, "r");
        test_int_eq(aept_deb_add_packages(ir, fp), 0, "an index parses");
        fclose(fp);

        FOR_REPO_SOLVABLES(ir, ip, s)
        n++;
        test_int_eq(n, 2, "the header stanza is not a package, the two others are");

        repo_free(ir, 1);
        free(text);

        /* An over-long line, and a readable field after it. */
        ir = repo_create(pool, "long");
        fp = open_memstream(&text, &len);
        fputs("Package: delta\nVersion: 1\nArchitecture: all\nDepends: ", fp);
        for (i = 0; i < 9000; i++)
            fputc('x', fp);
        fputs("\nSuggests: after\n\n", fp);
        fclose(fp);

        fp = fmemopen(text, len, "r");
        aept_deb_add_packages(ir, fp);
        fclose(fp);

        n = 0;
        FOR_REPO_SOLVABLES(ir, ip, s)
        n++;
        test_int_eq(n, 1, "a stanza carrying an over-long line is still a package");

        FOR_REPO_SOLVABLES(ir, ip, s)
        {
            got = deps_of(s, SOLVABLE_REQUIRES);
            test_str_eq(got, "", "the over-long field is dropped, not read in pieces");
            free(got);
            got = deps_of(s, SOLVABLE_SUGGESTS);
            test_str_eq(got, "after", "and the field after it is read normally");
            free(got);
        }

        repo_free(ir, 1);
        free(text);
    }

    /* A stanza naming no package is not a package.  This is what
     * discards the Origin/Valid-Until header an index opens with. */
    test_ok(aept_deb_add_control(repo, "Origin: Aeltra\nValid-Until: 2026-09-10T06:15:55Z\n") == 0,
            "a stanza with no Package field is not a solvable");

    pool_free(pool);
    return test_summary();
}
