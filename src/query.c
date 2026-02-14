/* query.c - read-only query commands
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <solv/evr.h>
#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/solvable.h>

#include "aept/aept.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/query.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

static const char *strip_leading(const char *p)
{
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    while (p[0] == '/')
        p++;
    return p;
}

/* Length excluding trailing slashes (but at least 1 for "/") */
static size_t path_len(const char *p)
{
    size_t len = strlen(p);

    while (len > 1 && p[len - 1] == '/')
        len--;
    return len;
}

int aept_owns(const char *path)
{
    DIR *dir;
    struct dirent *ent;
    const char *needle;
    size_t needle_len;
    int found = 0;

    if (*path == '\0') {
        log_error("empty file path");
        return 1;
    }
    needle = strip_leading(path);
    if (*needle == '\0')
        needle = ".";
    needle_len = path_len(needle);

    dir = opendir(cfg->info_dir);
    if (!dir)
        return 1;

    while ((ent = readdir(dir)) != NULL) {
        const char *dot;
        char *list_path = NULL;
        FILE *fp;
        char buf[4096];

        dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".list") != 0)
            continue;

        xasprintf(&list_path, "%s/%s", cfg->info_dir, ent->d_name);
        fp = fopen(list_path, "r");
        free(list_path);

        if (!fp)
            continue;

        while (fgets(buf, sizeof(buf), fp)) {
            const char *entry;
            char *tab;

            buf[strcspn(buf, "\n")] = '\0';

            tab = strchr(buf, '\t');
            if (tab)
                *tab = '\0';

            if (*buf == '\0')
                continue;

            entry = strip_leading(buf);
            if (*entry == '\0')
                entry = ".";

            if (path_len(entry) == needle_len &&
                strncmp(entry, needle, needle_len) == 0) {
                /* Print package name (filename minus .list suffix) */
                size_t name_len = (size_t)(dot - ent->d_name);
                printf("%.*s\n", (int)name_len, ent->d_name);
                found = 1;
                break;
            }
        }

        fclose(fp);
    }

    closedir(dir);

    return found ? 0 : 1;
}

int aept_files(const char *name)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];

    xasprintf(&list_path, "%s/%s.list", cfg->info_dir, name);

    fp = fopen(list_path, "r");
    if (!fp) {
        log_error("package '%s' is not installed", name);
        free(list_path);
        return 1;
    }
    free(list_path);

    while (fgets(buf, sizeof(buf), fp)) {
        char *tab;

        buf[strcspn(buf, "\n")] = '\0';

        tab = strchr(buf, '\t');
        if (tab)
            *tab = '\0';

        if (*buf == '\0')
            continue;

        printf("%s\n", buf);
    }

    fclose(fp);
    return 0;
}

int aept_print_architecture(void)
{
    int i;

    for (i = 0; i < cfg->narchs; i++)
        printf("%s\n", cfg->archs[i]);

    return 0;
}

/* ── shared helpers ────────────────────────────────────────────────── */

static int load_repos(void)
{
    int i;

    for (i = 0; i < cfg->nsources; i++) {
        char *list_path = NULL;
        FILE *fp;

        xasprintf(&list_path, "%s/%s", cfg->lists_dir, cfg->sources[i].name);

        fp = fopen(list_path, "r");
        if (!fp) {
            log_debug("cannot open package list '%s': %s",
                      list_path, strerror(errno));
            free(list_path);
            continue;
        }

        solver_load_repo(cfg->sources[i].name, fp, i);
        fclose(fp);
        free(list_path);
    }

    return 0;
}

/* ── list ──────────────────────────────────────────────────────────── */

struct list_entry {
    Id name_id;
    Solvable *avail;
    Solvable *installed;
};

static Pool *sort_pool;

static int cmp_list_entry(const void *a, const void *b)
{
    const struct list_entry *ea = a;
    const struct list_entry *eb = b;

    return strcmp(pool_id2str(sort_pool, ea->name_id),
                  pool_id2str(sort_pool, eb->name_id));
}

static struct list_entry *find_entry(struct list_entry *entries, int n, Id name_id)
{
    int i;

    for (i = 0; i < n; i++) {
        if (entries[i].name_id == name_id)
            return &entries[i];
    }
    return NULL;
}

int aept_list(const char *pattern, int filter_installed, int filter_upgradable)
{
    Pool *pool;
    Id p;
    Solvable *s;
    struct list_entry *entries = NULL;
    int nentries = 0, alloc = 0;
    int i;

    if (solver_init() < 0)
        return 1;

    status_load();
    load_repos();

    pool = solver_pool();

    /* Collect unique packages, tracking best available and installed */
    FOR_POOL_SOLVABLES(p) {
        struct list_entry *e;

        s = pool_id2solvable(pool, p);
        e = find_entry(entries, nentries, s->name);

        if (!e) {
            if (nentries >= alloc) {
                alloc = alloc ? alloc * 2 : 256;
                entries = xrealloc(entries, alloc * sizeof(*entries));
            }
            e = &entries[nentries++];
            e->name_id = s->name;
            e->avail = NULL;
            e->installed = NULL;
        }

        if (s->repo == pool->installed) {
            e->installed = s;
        } else {
            if (!e->avail || pool_evrcmp_str(pool,
                    pool_id2str(pool, s->evr),
                    pool_id2str(pool, e->avail->evr), EVRCMP_COMPARE) > 0)
                e->avail = s;
        }
    }

    sort_pool = pool;
    qsort(entries, nentries, sizeof(*entries), cmp_list_entry);

    for (i = 0; i < nentries; i++) {
        struct list_entry *e = &entries[i];
        const char *name = pool_id2str(pool, e->name_id);
        const char *summary;
        Solvable *show;
        int upgradable;

        if (pattern && fnmatch(pattern, name, 0) != 0)
            continue;

        if (filter_installed && !e->installed)
            continue;

        upgradable = e->installed && e->avail &&
            pool_evrcmp_str(pool,
                pool_id2str(pool, e->avail->evr),
                pool_id2str(pool, e->installed->evr), EVRCMP_COMPARE) > 0;

        if (filter_upgradable && !upgradable)
            continue;

        show = filter_installed ? e->installed :
               (e->avail ? e->avail : e->installed);

        printf("%s - %s", name, pool_id2str(pool, show->evr));

        summary = solvable_lookup_str(show, SOLVABLE_SUMMARY);
        if (summary)
            printf(" - %s", summary);

        if (e->installed) {
            if (upgradable)
                printf(" [installed,upgradable]");
            else
                printf(" [installed]");
        }

        printf("\n");
    }

    free(entries);
    solver_fini();
    return 0;
}

/* ── show ──────────────────────────────────────────────────────────── */

static void print_deparray(Pool *pool, Solvable *s, Id keyname, Id marker,
                           const char *label)
{
    Queue q;
    int i;

    queue_init(&q);
    solvable_lookup_deparray(s, keyname, &q, marker);

    if (q.count == 0) {
        queue_free(&q);
        return;
    }

    printf("%s:", label);
    for (i = 0; i < q.count; i++) {
        if (i > 0)
            printf(",");
        printf(" %s", pool_dep2str(pool, q.elements[i]));
    }
    printf("\n");
    queue_free(&q);
}

static void print_description(Pool *pool, Solvable *s)
{
    const char *summary = solvable_lookup_str(s, SOLVABLE_SUMMARY);
    const char *desc = solvable_lookup_str(s, SOLVABLE_DESCRIPTION);

    if (!summary)
        return;

    printf("Description: %s\n", summary);
    if (desc) {
        const char *p = desc;
        while (*p) {
            const char *eol = strchr(p, '\n');
            if (eol) {
                printf(" %.*s\n", (int)(eol - p), p);
                p = eol + 1;
            } else {
                printf(" %s\n", p);
                break;
            }
        }
    }
}

int aept_show(const char *name)
{
    Pool *pool;
    Id name_id, p;
    Solvable *s, *best = NULL, *installed = NULL;
    const char *str;
    unsigned long long num;
    unsigned int medianr;
    int r = 1;

    if (solver_init() < 0)
        return 1;

    status_load();
    load_repos();

    pool = solver_pool();

    name_id = pool_str2id(pool, name, 0);
    if (!name_id)
        goto not_found;

    FOR_POOL_SOLVABLES(p) {
        s = pool_id2solvable(pool, p);
        if (s->name != name_id)
            continue;

        if (s->repo == pool->installed) {
            installed = s;
        } else {
            if (!best || pool_evrcmp_str(pool,
                    pool_id2str(pool, s->evr),
                    pool_id2str(pool, best->evr), EVRCMP_COMPARE) > 0)
                best = s;
        }
    }

    s = best ? best : installed;
    if (!s)
        goto not_found;

    printf("Package: %s\n", pool_id2str(pool, s->name));
    printf("Version: %s\n", pool_id2str(pool, s->evr));
    printf("Architecture: %s\n", pool_id2str(pool, s->arch));

    num = solvable_lookup_num(s, SOLVABLE_INSTALLSIZE, 0);
    if (num)
        printf("Installed-Size: %llu kB\n", num);

    print_deparray(pool, s, SOLVABLE_REQUIRES, -SOLVABLE_PREREQMARKER,
                   "Depends");
    print_deparray(pool, s, SOLVABLE_REQUIRES, SOLVABLE_PREREQMARKER,
                   "Pre-Depends");
    print_deparray(pool, s, SOLVABLE_RECOMMENDS, 0, "Recommends");
    print_deparray(pool, s, SOLVABLE_SUGGESTS, 0, "Suggests");
    print_deparray(pool, s, SOLVABLE_PROVIDES, -SOLVABLE_FILEMARKER,
                   "Provides");
    print_deparray(pool, s, SOLVABLE_CONFLICTS, 0, "Conflicts");
    print_deparray(pool, s, SOLVABLE_OBSOLETES, 0, "Replaces");

    str = solvable_lookup_str(s, SOLVABLE_URL);
    if (str)
        printf("Homepage: %s\n", str);

    str = solvable_lookup_location(s, &medianr);
    if (str)
        printf("Filename: %s\n", str);

    print_description(pool, s);

    if (installed)
        printf("Status: install ok installed\n");

    r = 0;
    goto cleanup;

not_found:
    log_error("package '%s' not found", name);

cleanup:
    solver_fini();
    return r;
}
