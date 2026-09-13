/* deb.c - control stanzas into libsolv solvables
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/repo.h>
#include <solv/repodata.h>

#include "aept/deb.h"
#include "aept/msg.h"
#include "aept/stanza.h"
#include "aept/util.h"

/* Spelled like a control field rather than a libsolv key, so it cannot
 * collide with one libsolv defines later. */
#define AEPT_REPLACES_KEY "aept:replaces"

Id aept_deb_replaces_key(Pool *pool)
{
    return pool_str2id(pool, AEPT_REPLACES_KEY, 1);
}

/* Whether one of s's dep arrays names other, matching by name only. */
static int names(Pool *pool, Solvable *s, Id key, Id other)
{
    Queue q;
    int i, hit = 0;

    queue_init(&q);
    solvable_lookup_deparray(s, key, &q, 0);

    for (i = 0; i < q.count && !hit; i++) {
        Id dep = q.elements[i];

        if (ISRELDEP(dep)) {
            Reldep *rd = GETRELDEP(pool, dep);
            dep = rd->name;
        }
        hit = (dep == other);
    }

    queue_free(&q);
    return hit;
}

int aept_deb_takes_over(Pool *pool, Solvable *s, Id other)
{
    if (!other)
        return 0;

    return names(pool, s, aept_deb_replaces_key(pool), other) &&
           names(pool, s, SOLVABLE_CONFLICTS, other);
}

static int is_blank(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return *s == '\0';
}

/*
 * One entry: "name", "name (>= 1.0)", either optionally followed by
 * "| alternative".  Returns 0 for anything else, including an empty
 * entry; the caller tells those apart.
 *
 * Stricter than it needs to be on purpose.  A dependency that parses
 * into something smaller than it says -- a name with the relation
 * dropped, say -- installs a package against a version it was never
 * built for, and nothing downstream can notice.  Refusing is visible.
 */
static Id parse_dep(Pool *pool, const char *p)
{
    const char *n, *ne, *e = NULL, *ee = NULL, *op;
    Id name, evr;
    int flags = 0;

    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p)
        return 0;

    n = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '(' && *p != '|')
        p++;
    ne = p;
    if (ne == n)
        return 0;

    while (*p == ' ' || *p == '\t')
        p++;

    if (*p == '(') {
        p++;
        while (*p == ' ' || *p == '\t')
            p++;

        op = p;
        while (*p == '<' || *p == '>' || *p == '=')
            p++;

        /* "<" and ">" alone are Policy 7.1's deprecated spellings of
         * "<=" and ">="; ">>" and "<<" are the strict ones. */
        if (p - op == 1 && *op == '<') {
            flags = REL_LT | REL_EQ;
        } else if (p - op == 1 && *op == '>') {
            flags = REL_GT | REL_EQ;
        } else {
            for (; op < p; op++) {
                if (*op == '<')
                    flags |= REL_LT;
                else if (*op == '>')
                    flags |= REL_GT;
                else
                    flags |= REL_EQ;
            }
        }
        if (!flags)
            return 0;

        while (*p == ' ' || *p == '\t')
            p++;
        e = p;
        while (*p && *p != ')' && *p != ' ' && *p != '\t')
            p++;
        ee = p;
        if (ee == e)
            return 0;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p != ')')
            return 0;
        p++;
        while (*p == ' ' || *p == '\t')
            p++;
    }

    name = pool_strn2id(pool, n, ne - n, 1);
    if (flags) {
        evr = pool_strn2id(pool, e, ee - e, 1);
        name = pool_rel2id(pool, name, evr, flags, 1);
    }

    if (*p == '|') {
        Id alt = parse_dep(pool, p + 1);
        if (!alt)
            return 0;
        return pool_rel2id(pool, name, alt, REL_OR, 1);
    }

    /* Anything still here was not part of the grammar. */
    if (*p)
        return 0;

    return name;
}

/*
 * Split a field on "," and collect the entries.  Returns -1, having
 * reported which entry, if one of them is not a dependency.
 */
static int parse_dep_list(Pool *pool, const char *pkg, const char *fieldname, const char *value,
                          Queue *out)
{
    const char *p = value;

    while (*p) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char *entry = aept_malloc(len + 1);
        Id id;

        memcpy(entry, p, len);
        entry[len] = '\0';

        id = parse_dep(pool, entry);
        if (id)
            queue_push(out, id);
        else if (!is_blank(entry)) {
            aept_log_error("%s: cannot parse %s entry '%s'", pkg, fieldname, entry);
            free(entry);
            return -1;
        }
        free(entry);

        if (!end)
            break;
        p = end + 1;
    }

    return 0;
}

static int add_dep_field(Repo *repo, const char *stanza, const char *pkg, const char *fieldname,
                         Offset *deps, Id marker, Queue *scratch)
{
    char *value;
    int i, r = 0;

    value = aept_stanza_field(stanza, fieldname);
    if (!value)
        return 0;

    queue_empty(scratch);
    if (parse_dep_list(repo->pool, pkg, fieldname, value, scratch) < 0) {
        r = -1;
    } else {
        for (i = 0; i < scratch->count; i++)
            *deps = repo_addid_dep(repo, *deps, scratch->elements[i], marker);
    }

    free(value);
    return r;
}

static void set_num_field(Repodata *data, Id p, const char *stanza, const char *fieldname, Id key,
                          int shift)
{
    char *v = aept_stanza_field(stanza, fieldname);

    if (!v)
        return;
    repodata_set_num(data, p, key, strtoull(v, NULL, 10) << shift);
    free(v);
}

static void set_str_field(Repodata *data, Id p, const char *stanza, const char *fieldname, Id key)
{
    char *v = aept_stanza_field(stanza, fieldname);

    if (!v)
        return;
    repodata_set_str(data, p, key, v);
    free(v);
}

/*
 * Description is the one field whose line structure carries meaning:
 * the first line is the summary and the rest the body, so it is read
 * unfolded.  Every other field here is a single logical value.
 */
static void set_description(Repodata *data, Id p, const char *stanza)
{
    char *v = aept_stanza_field_lines(stanza, "Description");
    char *nl;

    if (!v)
        return;

    nl = strchr(v, '\n');
    if (nl) {
        *nl = '\0';
        repodata_set_str(data, p, SOLVABLE_DESCRIPTION, nl + 1);
    } else {
        repodata_set_str(data, p, SOLVABLE_DESCRIPTION, v);
    }
    repodata_set_str(data, p, SOLVABLE_SUMMARY, v);
    free(v);
}

static Id add_stanza(Repo *repo, Repodata *data, const char *stanza)
{
    Pool *pool = repo->pool;
    Solvable *s;
    Queue q;
    Id p;
    char *name, *v;
    int i, bad;

    name = aept_stanza_field(stanza, "Package");
    if (!name)
        return 0;

    p = repo_add_solvable(repo);
    s = pool_id2solvable(pool, p);
    queue_init(&q);

    s->name = pool_str2id(pool, name, 1);

    v = aept_stanza_field(stanza, "Version");
    s->evr = v ? pool_str2id(pool, v, 1) : ID_EMPTY;
    free(v);

    v = aept_stanza_field(stanza, "Architecture");
    s->arch = v ? pool_str2id(pool, v, 1) : ARCH_ALL;
    free(v);

    struct {
        const char *field;
        Offset *deps;
        Id marker;
    } fields[] = {
        /* Depends carries the marker negated and Pre-Depends plain,
         * which is what orders the pre-dependencies ahead of it in the
         * one array the two share. */
        {"Depends",     &s->requires,   -SOLVABLE_PREREQMARKER},
        {"Pre-Depends", &s->requires,   SOLVABLE_PREREQMARKER },
        {"Recommends",  &s->recommends, 0                     },
        {"Suggests",    &s->suggests,   0                     },
        {"Conflicts",   &s->conflicts,  0                     },
        /* Breaks is a conflict as far as solving goes. */
        {"Breaks",      &s->conflicts,  0                     },
        {"Provides",    &s->provides,   0                     },
    };

    bad = 0;
    for (i = 0; i < (int)(sizeof(fields) / sizeof(fields[0])) && !bad; i++)
        bad = add_dep_field(repo, stanza, name, fields[i].field, fields[i].deps, fields[i].marker,
                            &q) < 0;

    /* Replaces goes to aept's own key: see deb.h. */
    if (!bad) {
        v = aept_stanza_field(stanza, "Replaces");
        if (v) {
            queue_empty(&q);
            if (parse_dep_list(pool, name, "Replaces", v, &q) < 0)
                bad = 1;
            else if (q.count)
                repodata_set_idarray(data, p, aept_deb_replaces_key(pool), &q);
            free(v);
        }
    }

    queue_free(&q);

    if (bad) {
        aept_log_error("dropping package '%s'", name);
        solvable_free(s, 1);
        free(name);
        return 0;
    }

    /* The implicit self-provide, which is what makes a package
     * satisfy a dependency on its own name at its own version. */
    s->provides =
        repo_addid_dep(repo, s->provides, pool_rel2id(pool, s->name, s->evr, REL_EQ, 1), 0);

    v = aept_stanza_field(stanza, "Filename");
    if (v) {
        repodata_set_location(data, p, 0, NULL, v);
        free(v);
    }

    v = aept_stanza_field(stanza, "SHA256");
    if (v) {
        if (strlen(v) == 32 * 2)
            repodata_set_checksum(data, p, SOLVABLE_CHECKSUM, REPOKEY_TYPE_SHA256, v);
        else
            aept_log_warning("%s: ignoring malformed SHA256", name);
        free(v);
    }

    /* Installed-Size is in kB by the format's definition, and every
     * SOLVABLE_INSTALLSIZE reader expects bytes. */
    set_num_field(data, p, stanza, "Installed-Size", SOLVABLE_INSTALLSIZE, 10);
    set_num_field(data, p, stanza, "Size", SOLVABLE_DOWNLOADSIZE, 0);
    set_str_field(data, p, stanza, "Homepage", SOLVABLE_URL);
    set_description(data, p, stanza);

    free(name);
    return p;
}

struct add_state {
    Repo *repo;
    Repodata *data;
    int count;
};

static int add_one(const char *stanza, void *user)
{
    struct add_state *st = user;

    if (add_stanza(st->repo, st->data, stanza))
        st->count++;
    return 0;
}

int aept_deb_add_packages(Repo *repo, FILE *fp)
{
    struct add_state st;

    st.repo = repo;
    st.data = repo_add_repodata(repo, 0);
    st.count = 0;

    if (!st.data)
        return -1;

    aept_stanza_foreach(fp, add_one, &st);
    repodata_internalize(st.data);

    return 0;
}

Id aept_deb_add_control(Repo *repo, const char *control)
{
    Repodata *data = repo_add_repodata(repo, REPO_REUSE_REPODATA);
    Id p;

    if (!data)
        return 0;

    p = add_stanza(repo, data, control);
    repodata_internalize(data);

    return p;
}
