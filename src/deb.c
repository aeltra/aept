/* deb.c - control stanzas into libsolv solvables
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <ctype.h>
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
#define AEPT_CONFLICTS_KEY "aept:conflicts"

Id aept_deb_replaces_key(Pool *pool)
{
    return pool_str2id(pool, AEPT_REPLACES_KEY, 1);
}

Id aept_deb_conflicts_key(Pool *pool)
{
    return pool_str2id(pool, AEPT_CONFLICTS_KEY, 1);
}

/* The name a dependency is about, with any version relation dropped. */
static Id dep_name(Pool *pool, Id dep)
{
    if (ISRELDEP(dep)) {
        Reldep *rd = GETRELDEP(pool, dep);
        return rd->name;
    }
    return dep;
}

/* Whether other answers to name -- as its own name, or via Provides. */
static int offers(Pool *pool, Solvable *other, Id name)
{
    Queue q;
    int i, hit = 0;

    if (other->name == name)
        return 1;

    queue_init(&q);
    solvable_lookup_deparray(other, SOLVABLE_PROVIDES, &q, 0);
    for (i = 0; i < q.count && !hit; i++)
        hit = (dep_name(pool, q.elements[i]) == name);
    queue_free(&q);

    return hit;
}

/*
 * Whether the dep array under key names other.  With virtual set, a
 * name other merely provides counts; without it, only other's own name
 * does.  Policy 7.6 wants each rule in a different place.
 */
static int names_pkg(Pool *pool, Solvable *s, Id key, Solvable *other, int virtual)
{
    Queue q;
    int i, hit = 0;

    queue_init(&q);
    solvable_lookup_deparray(s, key, &q, 0);
    for (i = 0; i < q.count && !hit; i++) {
        Id n = dep_name(pool, q.elements[i]);

        hit = virtual ? offers(pool, other, n) : (n == other->name);
    }
    queue_free(&q);

    return hit;
}

aept_takeover_mode_t aept_deb_takeover(Pool *pool, Solvable *s, Solvable *other)
{
    if (!s || !other)
        return AEPT_TAKEOVER_NONE;

    /*
     * Conflicts decides which of Policy 7.6's two readings applies, so
     * it is asked first and its answer is final either way: a conflict
     * without a matching Replaces is an ordinary clash, not a licence
     * to fall through to 7.6.1.
     */
    if (names_pkg(pool, s, aept_deb_conflicts_key(pool), other, 1)) {
        if (names_pkg(pool, s, aept_deb_replaces_key(pool), other, 1))
            return AEPT_TAKEOVER_SUPERSEDE;
        return AEPT_TAKEOVER_NONE;
    }

    if (names_pkg(pool, s, aept_deb_replaces_key(pool), other, 0))
        return AEPT_TAKEOVER_OVERWRITE;

    return AEPT_TAKEOVER_NONE;
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

/* Parse one dependency field into deps, through the scratch queue. */
static int add_deps(Repo *repo, Offset *deps, Id marker, const char *pkg, const char *fieldname,
                    const char *value, Queue *scratch)
{
    int i;

    queue_empty(scratch);
    if (parse_dep_list(repo->pool, pkg ? pkg : "?", fieldname, value, scratch) < 0)
        return -1;

    for (i = 0; i < scratch->count; i++)
        *deps = repo_addid_dep(repo, *deps, scratch->elements[i], marker);

    return 0;
}

enum {
    F_NONE = 0,
    F_PACKAGE,
    F_VERSION,
    F_ARCHITECTURE,
    F_DEPENDS,
    F_PREDEPENDS,
    F_RECOMMENDS,
    F_SUGGESTS,
    F_BREAKS,
    F_PROVIDES,
    F_CONFLICTS,
    F_REPLACES,
    F_FILENAME,
    F_SHA256,
    F_INSTALLEDSIZE,
    F_SIZE,
    F_HOMEPAGE,
    F_DESCRIPTION,
};

/* Lowercase, so field_id() can reject most entries on one character
 * rather than a call. */
static const struct {
    const char *name;
    int id;
} known_fields[] = {
    {"package",        F_PACKAGE      },
    {"version",        F_VERSION      },
    {"architecture",   F_ARCHITECTURE },
    {"depends",        F_DEPENDS      },
    {"pre-depends",    F_PREDEPENDS   },
    {"recommends",     F_RECOMMENDS   },
    {"suggests",       F_SUGGESTS     },
    {"breaks",         F_BREAKS       },
    {"provides",       F_PROVIDES     },
    {"conflicts",      F_CONFLICTS    },
    {"replaces",       F_REPLACES     },
    {"filename",       F_FILENAME     },
    {"sha256",         F_SHA256       },
    {"installed-size", F_INSTALLEDSIZE},
    {"size",           F_SIZE         },
    {"homepage",       F_HOMEPAGE     },
    {"description",    F_DESCRIPTION  },
    {NULL,             F_NONE         },
};

static int field_id(const aept_stanza_field_t *f)
{
    int c, i;

    if (!f->name_len)
        return F_NONE;

    c = tolower((unsigned char)f->name[0]);
    for (i = 0; known_fields[i].name; i++)
        if (known_fields[i].name[0] == c && aept_stanza_field_is(f, known_fields[i].name))
            return known_fields[i].id;

    return F_NONE;
}

/*
 * One stanza as a solvable, in a single pass.
 *
 * Everything destined for repodata is held aside until the whole stanza
 * has parsed, rather than written as it is read: a repodata entry
 * written for a solvable that is then freed would be inherited by the
 * next one, because solvable_free() hands the id straight back.
 */
static Id add_stanza(Repo *repo, Repodata *data, const char *stanza, aept_stanza_buf_t *vb)
{
    Pool *pool = repo->pool;
    Solvable *s;
    Queue q, conf, repl;
    Id p;
    const char *pos = stanza;
    aept_stanza_field_t f;
    char *pkg = NULL, *filename = NULL, *sha256 = NULL, *homepage = NULL, *descr = NULL;
    unsigned long long isize = 0, dsize = 0;
    int have_isize = 0, have_dsize = 0, bad = 0, i;

    p = repo_add_solvable(repo);
    s = pool_id2solvable(pool, p);
    queue_init(&q);
    queue_init(&conf);
    queue_init(&repl);

    while (!bad && aept_stanza_next_field(&pos, &f)) {
        int id = field_id(&f);
        const char *fname, *v;

        if (id == F_NONE)
            continue;

        /*
         * Description is the one field whose line structure carries
         * meaning; every other is a single logical value.
         *
         * The value lands in the shared buffer, so it is good only
         * until the next field is read.  Most are used and finished
         * with inside this iteration; the five kept for the commit
         * below are copied out of it.
         */
        v = aept_stanza_value_into(&f, id == F_DESCRIPTION, vb);
        fname = known_fields[id - 1].name;

        switch (id) {
        case F_PACKAGE:
            free(pkg);
            pkg = aept_strdup(v);
            s->name = pool_str2id(pool, pkg, 1);
            break;
        case F_VERSION:
            s->evr = pool_str2id(pool, v, 1);
            break;
        case F_ARCHITECTURE:
            s->arch = pool_str2id(pool, v, 1);
            break;
        case F_DEPENDS:
            /* Depends carries the marker negated and Pre-Depends plain,
             * which is what orders the pre-dependencies ahead of it in
             * the one array the two share. */
            bad = add_deps(repo, &s->requires, -SOLVABLE_PREREQMARKER, pkg, fname, v, &q) < 0;
            break;
        case F_PREDEPENDS:
            bad = add_deps(repo, &s->requires, SOLVABLE_PREREQMARKER, pkg, fname, v, &q) < 0;
            break;
        case F_RECOMMENDS:
            bad = add_deps(repo, &s->recommends, 0, pkg, fname, v, &q) < 0;
            break;
        case F_SUGGESTS:
            bad = add_deps(repo, &s->suggests, 0, pkg, fname, v, &q) < 0;
            break;
        case F_BREAKS:
            /* A conflict as far as solving goes, but not recorded as
             * one: see aept_deb_takeover(). */
            bad = add_deps(repo, &s->conflicts, 0, pkg, fname, v, &q) < 0;
            break;
        case F_PROVIDES:
            bad = add_deps(repo, &s->provides, 0, pkg, fname, v, &q) < 0;
            break;
        case F_CONFLICTS:
            queue_empty(&conf);
            bad = parse_dep_list(pool, pkg ? pkg : "?", fname, v, &conf) < 0;
            break;
        case F_REPLACES:
            queue_empty(&repl);
            bad = parse_dep_list(pool, pkg ? pkg : "?", fname, v, &repl) < 0;
            break;
        case F_FILENAME:
            free(filename);
            filename = aept_strdup(v);
            break;
        case F_SHA256:
            free(sha256);
            sha256 = aept_strdup(v);
            break;
        case F_INSTALLEDSIZE:
            isize = strtoull(v, NULL, 10);
            have_isize = 1;
            break;
        case F_SIZE:
            dsize = strtoull(v, NULL, 10);
            have_dsize = 1;
            break;
        case F_HOMEPAGE:
            free(homepage);
            homepage = aept_strdup(v);
            break;
        case F_DESCRIPTION:
            free(descr);
            descr = aept_strdup(v);
            break;
        default:
            break;
        }
    }

    queue_free(&q);

    /* A stanza naming no package is not one -- which is what discards
     * the Origin/Valid-Until header an index opens with. */
    if (bad || !s->name) {
        if (bad)
            aept_log_error("dropping package '%s'", pkg ? pkg : "(unnamed)");
        solvable_free(s, 1);
        queue_free(&conf);
        queue_free(&repl);
        free(pkg);
        free(filename);
        free(sha256);
        free(homepage);
        free(descr);
        return 0;
    }

    if (!s->evr)
        s->evr = ID_EMPTY;
    if (!s->arch)
        s->arch = ARCH_ALL;

    /* Conflicts reaches the solver like Breaks, and is kept apart from
     * it under aept's own key: see deb.h. */
    for (i = 0; i < conf.count; i++)
        s->conflicts = repo_addid_dep(repo, s->conflicts, conf.elements[i], 0);
    if (conf.count)
        repodata_set_idarray(data, p, aept_deb_conflicts_key(pool), &conf);
    if (repl.count)
        repodata_set_idarray(data, p, aept_deb_replaces_key(pool), &repl);
    queue_free(&conf);
    queue_free(&repl);

    /* The implicit self-provide, which is what makes a package satisfy
     * a dependency on its own name at its own version. */
    s->provides =
        repo_addid_dep(repo, s->provides, pool_rel2id(pool, s->name, s->evr, REL_EQ, 1), 0);

    if (filename)
        repodata_set_location(data, p, 0, NULL, filename);

    if (sha256) {
        if (strlen(sha256) == 32 * 2)
            repodata_set_checksum(data, p, SOLVABLE_CHECKSUM, REPOKEY_TYPE_SHA256, sha256);
        else
            aept_log_warning("%s: ignoring malformed SHA256", pkg);
    }

    /* Installed-Size is in kB by the format's definition, and every
     * SOLVABLE_INSTALLSIZE reader expects bytes. */
    if (have_isize)
        repodata_set_num(data, p, SOLVABLE_INSTALLSIZE, isize << 10);
    if (have_dsize)
        repodata_set_num(data, p, SOLVABLE_DOWNLOADSIZE, dsize);
    if (homepage)
        repodata_set_str(data, p, SOLVABLE_URL, homepage);

    if (descr) {
        char *nl = strchr(descr, '\n');

        if (nl) {
            *nl = '\0';
            repodata_set_str(data, p, SOLVABLE_DESCRIPTION, nl + 1);
        } else {
            repodata_set_str(data, p, SOLVABLE_DESCRIPTION, descr);
        }
        repodata_set_str(data, p, SOLVABLE_SUMMARY, descr);
    }

    free(pkg);
    free(filename);
    free(sha256);
    free(homepage);
    free(descr);
    return p;
}

struct add_state {
    Repo *repo;
    Repodata *data;
    aept_stanza_buf_t vb;
    int count;
};

static int add_one(const char *stanza, void *user)
{
    struct add_state *st = user;

    if (add_stanza(st->repo, st->data, stanza, &st->vb))
        st->count++;
    return 0;
}

int aept_deb_add_packages(Repo *repo, FILE *fp)
{
    struct add_state st;

    memset(&st, 0, sizeof(st));
    st.repo = repo;
    st.data = repo_add_repodata(repo, REPO_REUSE_REPODATA);

    if (!st.data)
        return -1;

    /* One buffer for the whole index: every field of every stanza
     * passes through it, so it grows to the largest value once and no
     * field allocates after that. */
    aept_stanza_foreach(fp, add_one, &st);
    aept_stanza_buf_free(&st.vb);
    repodata_internalize(st.data);

    return 0;
}

Id aept_deb_add_control(Repo *repo, const char *control)
{
    Repodata *data = repo_add_repodata(repo, REPO_REUSE_REPODATA);
    aept_stanza_buf_t vb = {NULL, 0};
    Id p;

    if (!data)
        return 0;

    p = add_stanza(repo, data, control, &vb);
    aept_stanza_buf_free(&vb);
    repodata_internalize(data);

    return p;
}
