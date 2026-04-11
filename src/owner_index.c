/* owner_index.c - file path → owning package index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/internal.h"
#include "aept/owner_index.h"
#include "aept/util.h"

void aept_owner_index_init(aept_owner_index_t *idx)
{
    memset(idx, 0, sizeof(*idx));
}

void aept_owner_index_free(aept_owner_index_t *idx)
{
    int i;

    for (i = 0; i < idx->count; i++)
        free(idx->entries[i].path);
    free(idx->entries);

    for (i = 0; i < idx->n_recent; i++)
        free(idx->recent[i].path);
    free(idx->recent);

    for (i = 0; i < idx->n_owners; i++)
        free(idx->owners[i]);
    free(idx->owners);

    for (i = 0; i < idx->n_dead; i++)
        free((char *)idx->dead[i]);
    free(idx->dead);

    memset(idx, 0, sizeof(*idx));
}

/* Strip leading "./" and "/" so paths match regardless of form. */
static const char *strip_leading(const char *path)
{
    while (path[0] == '.' && path[1] == '/')
        path += 2;
    while (path[0] == '/')
        path++;
    return path;
}

/* Return a live interned pointer for name, creating one if needed.
 * Dead (dropped) owners are never returned. */
static const char *intern_owner(aept_owner_index_t *idx, const char *name)
{
    int i;

    for (i = 0; i < idx->n_owners; i++) {
        if (strcmp(idx->owners[i], name) == 0)
            return idx->owners[i];
    }

    if (idx->n_owners >= idx->owners_alloc) {
        idx->owners_alloc = idx->owners_alloc ? idx->owners_alloc * 2 : 64;
        idx->owners = aept_realloc(idx->owners,
                                   idx->owners_alloc * sizeof(char *));
    }

    idx->owners[idx->n_owners] = aept_strdup(name);
    return idx->owners[idx->n_owners++];
}

static int entry_cmp(const void *a, const void *b)
{
    const aept_owner_entry_t *ea = a;
    const aept_owner_entry_t *eb = b;
    return strcmp(ea->path, eb->path);
}

static int is_dead(aept_owner_index_t *idx, const char *owner)
{
    int i;

    for (i = 0; i < idx->n_dead; i++) {
        if (idx->dead[i] == owner)
            return 1;
    }
    return 0;
}

static void array_append(aept_owner_entry_t **arr, int *count, int *alloc,
                         const char *path, const char *owner)
{
    if (*count >= *alloc) {
        *alloc = *alloc ? *alloc * 2 : 256;
        *arr = aept_realloc(*arr, *alloc * sizeof(**arr));
    }
    (*arr)[*count].path = aept_strdup(path);
    (*arr)[*count].owner = owner;
    (*count)++;
}

/* Read list_path and append its normalized entries into arr. */
static void read_list_into(const char *list_path, const char *owner,
                           aept_owner_entry_t **arr, int *count, int *alloc)
{
    FILE *fp = fopen(list_path, "r");
    if (!fp)
        return;

    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }

        buf[strcspn(buf, "\n")] = '\0';

        char *tab = strchr(buf, '\t');
        if (tab)
            *tab = '\0';

        const char *p = strip_leading(buf);
        if (p[0] == '\0')
            continue;

        array_append(arr, count, alloc, p, owner);
    }

    fclose(fp);
}

int aept_owner_index_build(struct aept_ctx *ctx, aept_owner_index_t *idx)
{
    DIR *dir = opendir(ctx->config.info_dir);
    if (!dir)
        return 0;     /* empty or missing info dir is fine */

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".list") != 0)
            continue;

        size_t name_len = (size_t)(dot - ent->d_name);
        char *name = aept_malloc(name_len + 1);
        memcpy(name, ent->d_name, name_len);
        name[name_len] = '\0';

        const char *owner = intern_owner(idx, name);
        free(name);

        char *list_path = NULL;
        aept_asprintf(&list_path, "%s/%s", ctx->config.info_dir, ent->d_name);

        read_list_into(list_path, owner,
                       &idx->entries, &idx->count, &idx->alloc);
        free(list_path);
    }
    closedir(dir);

    qsort(idx->entries, idx->count, sizeof(*idx->entries), entry_cmp);
    return 0;
}

const char *aept_owner_index_find(aept_owner_index_t *idx, const char *path)
{
    path = strip_leading(path);
    if (path[0] == '\0')
        return NULL;

    aept_owner_entry_t key;
    key.path = (char *)path;
    key.owner = NULL;

    /* Recent additions reflect the current transaction state, so they
     * take precedence over the original build-time snapshot. */
    if (idx->n_recent > 0) {
        aept_owner_entry_t *hit = bsearch(&key, idx->recent, idx->n_recent,
                                          sizeof(*idx->recent), entry_cmp);
        if (hit && !is_dead(idx, hit->owner))
            return hit->owner;
    }

    if (idx->count > 0) {
        aept_owner_entry_t *hit = bsearch(&key, idx->entries, idx->count,
                                          sizeof(*idx->entries), entry_cmp);
        if (hit && !is_dead(idx, hit->owner))
            return hit->owner;
    }

    return NULL;
}

int aept_owner_index_add_owner_files(aept_owner_index_t *idx,
                                     const char *owner_name,
                                     const char *list_path)
{
    const char *owner = intern_owner(idx, owner_name);

    read_list_into(list_path, owner,
                   &idx->recent, &idx->n_recent, &idx->recent_alloc);

    qsort(idx->recent, idx->n_recent, sizeof(*idx->recent), entry_cmp);
    return 0;
}

void aept_owner_index_drop_owner(aept_owner_index_t *idx,
                                 const char *owner_name)
{
    int i = 0;

    while (i < idx->n_owners) {
        if (strcmp(idx->owners[i], owner_name) != 0) {
            i++;
            continue;
        }

        if (idx->n_dead >= idx->dead_alloc) {
            idx->dead_alloc = idx->dead_alloc ? idx->dead_alloc * 2 : 16;
            idx->dead = aept_realloc(idx->dead,
                                     idx->dead_alloc * sizeof(*idx->dead));
        }
        idx->dead[idx->n_dead++] = idx->owners[i];

        /* Retire the live pointer so a subsequent intern_owner() for
         * the same name allocates a fresh, non-dead pointer. */
        idx->owners[i] = idx->owners[--idx->n_owners];
    }
}
