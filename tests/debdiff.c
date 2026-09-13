/* debdiff.c - aept's control parser against libsolv's, side by side.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Not part of "make check", and not built by it.  aept does not link
 * libsolvext any more -- src/deb.c replaced the one function it used --
 * so this harness would drag a dependency back in for every build to
 * serve a check that needs a real archive index to be worth running.
 *
 * Build and run it by hand after changing the parser; the recipe is in
 * CLAUDE.md.  It loads one index both ways and compares the resulting
 * solvables field by field.  Every difference is a finding except
 * SOLVABLE_DOWNLOADSIZE, which libsolv fills only when it stats a .deb
 * and never reads from an index's Size: field.
 */

#include <stdlib.h>
#include <string.h>
#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/repo_deb.h>
#include <solv/solvable.h>
#include <solv/queue.h>
#include <solv/knownid.h>
#include "aept/deb.h"

static int diffs;

static void dump(Pool *pool, Solvable *s, Id key, Queue *q)
{
    queue_empty(q);
    solvable_lookup_deparray(s, key, q, 0);
}

static void cmp(Pool *pa, Solvable *a, Pool *pb, Solvable *b, Id key, const char *what)
{
    Queue qa, qb;
    int i;

    queue_init(&qa);
    queue_init(&qb);
    dump(pa, a, key, &qa);
    dump(pb, b, key, &qb);

    if (qa.count != qb.count) {
        printf("%s: %s count %d vs %d\n", pool_id2str(pa, a->name), what, qa.count, qb.count);
        for (i = 0; i < qa.count; i++)
            printf("    solv: %s\n", pool_dep2str(pa, qa.elements[i]));
        for (i = 0; i < qb.count; i++)
            printf("    aept: %s\n", pool_dep2str(pb, qb.elements[i]));
        diffs++;
        goto out;
    }
    for (i = 0; i < qa.count; i++) {
        const char *sa = pool_dep2str(pa, qa.elements[i]);
        char *ca = strdup(sa);
        const char *sb = pool_dep2str(pb, qb.elements[i]);
        if (strcmp(ca, sb) != 0) {
            printf("%s: %s[%d] '%s' vs '%s'\n", pool_id2str(pa, a->name), what, i, ca, sb);
            diffs++;
        }
        free(ca);
    }
out:
    queue_free(&qa);
    queue_free(&qb);
}

static void cmpstr(Pool *pa, Solvable *a, Pool *pb, Solvable *b, Id key, const char *what)
{
    const char *va = solvable_lookup_str(a, key);
    const char *vb = solvable_lookup_str(b, key);
    if (!va && !vb)
        return;
    if (!va || !vb || strcmp(va, vb)) {
        printf("%s: %s '%s' vs '%s'\n", pool_id2str(pa, a->name), what, va ? va : "(none)",
               vb ? vb : "(none)");
        diffs++;
    }
}

static void cmpnum(Pool *pa, Solvable *a, Solvable *b, Id key, const char *what)
{
    unsigned long long na = solvable_lookup_num(a, key, 0);
    unsigned long long nb = solvable_lookup_num(b, key, 0);
    if (na != nb) {
        printf("%s: %s %llu vs %llu\n", pool_id2str(pa, a->name), what, na, nb);
        diffs++;
    }
}

int main(int argc, char **argv)
{
    Pool *pa = pool_create(), *pb = pool_create();
    /* Both pools on Debian semantics, as aept's solver sets them. */
    Repo *ra, *rb;
    FILE *fp;
    Id p;
    int n = 0;

    pool_setdisttype(pa, DISTTYPE_DEB);
    pool_setdisttype(pb, DISTTYPE_DEB);
    ra = repo_create(pa, "solv");
    rb = repo_create(pb, "aept");

    fp = fopen(argv[1], "r");
    repo_add_debpackages(ra, fp, 0);
    fclose(fp);

    fp = fopen(argv[1], "r");
    aept_deb_add_packages(rb, fp);
    fclose(fp);

    printf("solvables: libsolv %d, aept %d\n", ra->nsolvables, rb->nsolvables);
    if (ra->nsolvables != rb->nsolvables) {
        diffs++;
    }

    Solvable *a, *c;
    FOR_REPO_SOLVABLES(ra, p, a)
    {
        Solvable *b = NULL;
        Id q;
        FOR_REPO_SOLVABLES(rb, q, c)
        {
            if (!strcmp(pool_id2str(pb, c->name), pool_id2str(pa, a->name)) &&
                !strcmp(pool_id2str(pb, c->evr), pool_id2str(pa, a->evr))) {
                b = c;
                break;
            }
        }
        if (!b) {
            printf("%s: missing in aept\n", pool_id2str(pa, a->name));
            diffs++;
            continue;
        }
        n++;

        if (strcmp(pool_id2str(pa, a->arch), pool_id2str(pb, b->arch))) {
            printf("%s: arch differs\n", pool_id2str(pa, a->name));
            diffs++;
        }
        cmp(pa, a, pb, b, SOLVABLE_REQUIRES, "requires");
        cmp(pa, a, pb, b, SOLVABLE_PROVIDES, "provides");
        cmp(pa, a, pb, b, SOLVABLE_CONFLICTS, "conflicts");
        cmp(pa, a, pb, b, SOLVABLE_RECOMMENDS, "recommends");
        cmp(pa, a, pb, b, SOLVABLE_SUGGESTS, "suggests");
        cmpstr(pa, a, pb, b, SOLVABLE_SUMMARY, "summary");
        cmpstr(pa, a, pb, b, SOLVABLE_DESCRIPTION, "description");
        cmpnum(pa, a, b, SOLVABLE_INSTALLSIZE, "installsize");
        cmpnum(pa, a, b, SOLVABLE_DOWNLOADSIZE, "downloadsize");
        {
            unsigned int m1, m2;
            const char *la = solvable_lookup_location(a, &m1);
            const char *lb = solvable_lookup_location(b, &m2);
            if ((la || lb) && (!la || !lb || strcmp(la, lb))) {
                printf("%s: location '%s' vs '%s'\n", pool_id2str(pa, a->name), la ? la : "(none)",
                       lb ? lb : "(none)");
                diffs++;
            }
        }
    }
    printf("compared %d solvables, %d differences\n", n, diffs);
    (void)argc;
    return diffs ? 1 : 0;
}
