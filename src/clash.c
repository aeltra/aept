/* clash.c - file clash detection
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <solv/pool.h>
#include <solv/solvable.h>

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/clash.h"
#include "aept/deb.h"
#include "aept/msg.h"
#include "aept/owner_index.h"
#include "aept/remove.h"
#include "aept/util.h"

/*
 * The solvable describing the package that owns a path.  Preferring the
 * installed repo gives the version actually on disk; a package put
 * there earlier in this same transaction is not in that repo yet, so
 * any copy will do -- only the name and Provides are read, and those do
 * not differ between a repo's copy and the installed one.
 *
 * Linear, but reached only for a path two packages both ship, which is
 * rare and already an error in every case but a declared takeover.
 */
static Solvable *owner_solvable(Pool *pool, const char *owner_name)
{
    Id nid = pool_str2id(pool, owner_name, 0);
    Solvable *any = NULL;
    Id p;

    if (!nid)
        return NULL;

    FOR_POOL_SOLVABLES(p)
    {
        Solvable *s = pool_id2solvable(pool, p);

        if (s->name != nid || !s->repo)
            continue;
        if (s->repo == pool->installed)
            return s;
        if (!any)
            any = s;
    }

    return any;
}

void aept_takeover_list_init(aept_takeover_list_t *tl)
{
    tl->entries = NULL;
    tl->count = 0;
    tl->alloc = 0;
}

void aept_takeover_list_free(aept_takeover_list_t *tl)
{
    int i;

    for (i = 0; i < tl->count; i++) {
        free(tl->entries[i].owner);
        free(tl->entries[i].path);
    }
    free(tl->entries);
    aept_takeover_list_init(tl);
}

static void takeover_add(aept_takeover_list_t *tl, const char *owner, const char *path)
{
    if (tl->count == tl->alloc) {
        tl->alloc = tl->alloc ? tl->alloc * 2 : 8;
        tl->entries = aept_realloc(tl->entries, tl->alloc * sizeof(*tl->entries));
    }
    tl->entries[tl->count].owner = aept_strdup(owner);
    tl->entries[tl->count].path = aept_strdup(path);
    tl->count++;
}

/* Check whether an on-disk symlink and an archive symlink point to the
 * same target, and that target is a directory.  These are treated like
 * shared directories and are not conflicts. */
static int same_dir_symlink(const char *disk_path, const char *archive_target)
{
    char link_buf[4096];
    ssize_t len;
    char *resolved;
    struct stat st;
    int is_dir;

    if (!archive_target)
        return 0;

    len = readlink(disk_path, link_buf, sizeof(link_buf) - 1);
    if (len < 0)
        return 0;
    link_buf[len] = '\0';

    if (strcmp(link_buf, archive_target) != 0)
        return 0;

    resolved = realpath(disk_path, NULL);
    if (!resolved)
        return 0;

    is_dir = (lstat(resolved, &st) == 0 && S_ISDIR(st.st_mode));
    free(resolved);
    return is_dir;
}

int aept_clash_check(struct aept_ctx *ctx, const char *ipk_path, Pool *pool, Id p,
                     aept_fileset_t *old_files, aept_owner_index_t *owners,
                     aept_takeover_list_t *taken)
{
    Solvable *s = pool_id2solvable(pool, p);
    const char *pkg_name = pool_id2str(pool, s->name);
    aept_ar_file_list_t new_files;
    int clashes = 0;
    int i;

    aept_ar_file_list_init(&new_files);

    if (aept_ar_list_data_paths(ipk_path, ctx->config.ignore_uid, &new_files) < 0) {
        aept_ar_file_list_free(&new_files);
        return -1;
    }

    for (i = 0; i < new_files.count; i++) {
        const char *path = new_files.entries[i].path;
        const char *link_target = new_files.entries[i].link_target;
        const char *stripped = path;
        char *disk_path = NULL;
        struct stat st;
        const char *owner;

        while (stripped[0] == '.' && stripped[1] == '/')
            stripped += 2;
        while (stripped[0] == '/')
            stripped++;
        if (stripped[0] == '\0')
            continue;

        aept_asprintf(&disk_path, "%s/%s", ctx->config.offline_root ? ctx->config.offline_root : "",
                      stripped);

        if (lstat(disk_path, &st) < 0) {
            free(disk_path);
            continue;
        }

        /* Both are symlinks to the same directory — shared like dirs */
        if (S_ISLNK(st.st_mode) && link_target && same_dir_symlink(disk_path, link_target)) {
            free(disk_path);
            continue;
        }

        free(disk_path);

        /* Expected from old version of this package */
        if (old_files && aept_fileset_contains(old_files, path))
            continue;

        owner = owners ? aept_owner_index_find(owners, path) : NULL;
        if (!owner)
            continue;

        /* Same package (reinstall) */
        if (strcmp(owner, pkg_name) == 0)
            continue;

        switch (aept_deb_takeover(pool, s, owner_solvable(pool, owner))) {
        case AEPT_TAKEOVER_SUPERSEDE:
            /* The owner is being removed in this transaction anyway.
             * solver.c has ordered that removal after this install, and
             * the fileset threaded into it stops the removal deleting
             * the path it just handed over. */
            continue;
        case AEPT_TAKEOVER_OVERWRITE:
            /* The owner stays installed, so it has to stop claiming the
             * path -- recorded here, applied once the install has
             * succeeded. */
            takeover_add(taken, owner, stripped);
            continue;
        case AEPT_TAKEOVER_NONE:
            break;
        }

        aept_log_error("package '%s' wants to install '%s'\n"
                       "  but that file is already provided by package '%s'",
                       pkg_name, stripped, owner);
        clashes++;
    }

    aept_ar_file_list_free(&new_files);
    return clashes;
}

/*
 * Whether an installed package other than the owner itself depends on
 * the owner -- through any name it provides -- with nothing else
 * installed to satisfy that dependency.  The overwriter counts as
 * installed: it is on disk by the time this is asked, though not yet
 * in the installed repo.  A package erased earlier in this transaction
 * still is, so a dependency it satisfied reads as satisfied; that errs
 * towards keeping the owner, which costs an empty package and nothing
 * else.
 *
 * Recommends counts as a dependency here, as it does for dpkg.
 */
static int still_depended_on(Pool *pool, Solvable *owner, Id overwriter)
{
    Id owner_id = pool_solvable2id(pool, owner);
    Solvable *s;
    Id p;

    if (!pool->installed)
        return 0;

    FOR_REPO_SOLVABLES(pool->installed, p, s)
    {
        Offset offs[2] = {s->requires, s->recommends};
        int k;

        if (p == owner_id || s->name == owner->name)
            continue;

        for (k = 0; k < 2; k++) {
            Id *depp, dep;

            if (!offs[k])
                continue;

            depp = s->repo->idarraydata + offs[k];
            while ((dep = *depp++) != 0) {
                Id q, qq;
                int by_owner = 0, by_other = 0;

                if (dep == SOLVABLE_PREREQMARKER)
                    continue;

                FOR_PROVIDES(q, qq, dep)
                {
                    Solvable *qs = pool_id2solvable(pool, q);

                    if (q == owner_id)
                        by_owner = 1;
                    else if (q == overwriter || qs->repo == pool->installed)
                        by_other = 1;
                }

                if (by_owner && !by_other)
                    return 1;
            }
        }
    }

    return 0;
}

void aept_clash_commit_takeovers(struct aept_ctx *ctx, aept_takeover_list_t *taken, Pool *pool,
                                 Id overwriter, aept_owner_index_t *owners)
{
    Solvable *ws = pool_id2solvable(pool, overwriter);
    const char *overwriter_name = pool_id2str(pool, ws->name);
    const char *overwriter_version = pool_id2str(pool, ws->evr);
    int i, j;

    for (i = 0; i < taken->count; i++) {
        const char *owner = taken->entries[i].owner;
        aept_fileset_t drop;
        char *list_path = NULL, *tmp_path = NULL;
        FILE *in, *out;
        char line[4096];
        int failed = 0, kept = 0;

        /* Entries are grouped by owner as they are read, so an owner
         * already handled in an earlier pass is skipped here. */
        for (j = 0; j < i; j++)
            if (strcmp(taken->entries[j].owner, owner) == 0)
                break;
        if (j < i)
            continue;

        aept_fileset_init(&drop);
        for (j = i; j < taken->count; j++)
            if (strcmp(taken->entries[j].owner, owner) == 0)
                aept_fileset_add(&drop, taken->entries[j].path);
        aept_fileset_sort(&drop);

        aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, owner);
        aept_asprintf(&tmp_path, "%s/%s.list.tmp", ctx->config.info_dir, owner);

        in = fopen(list_path, "r");
        out = in ? fopen(tmp_path, "w") : NULL;
        if (!in || !out) {
            aept_log_warning("cannot rewrite file list '%s': %s", list_path, strerror(errno));
            failed = 1;
        }

        while (!failed && fgets(line, sizeof(line), in)) {
            char *tab, keep[sizeof(line)];
            const char *path;

            if (aept_fgets_is_truncated(line, sizeof(line))) {
                aept_fgets_drain_line(in);
                continue;
            }

            memcpy(keep, line, sizeof(keep));
            keep[strcspn(keep, "\n")] = '\0';
            tab = strchr(keep, '\t');
            if (tab)
                *tab = '\0';

            path = keep;
            while (path[0] == '.' && path[1] == '/')
                path += 2;
            while (path[0] == '/')
                path++;

            if (path[0] != '\0' && aept_fileset_contains(&drop, path))
                continue;

            /* Directories are shared and owned by everyone who ships
             * into them, so a list holding nothing else describes a
             * package that has no files left. */
            if (tab && !S_ISDIR((mode_t)strtoul(tab + 1, NULL, 8)))
                kept++;

            if (fputs(line, out) == EOF) {
                failed = 1;
                break;
            }
        }

        if (in)
            fclose(in);
        if (out && (ferror(out) | (fclose(out) != 0)))
            failed = 1;

        if (failed || rename(tmp_path, list_path) != 0) {
            if (!failed)
                aept_log_warning("cannot replace file list '%s': %s", list_path, strerror(errno));
            unlink(tmp_path);
        } else {
            aept_log_debug("'%s' disowned %d path(s) taken over", owner, drop.count);

            /* Nothing of it is left on disk, so it is not installed any
             * more -- whatever its status file still says -- unless
             * something depends on it, in which case it stays, empty. */
            if (kept == 0) {
                Solvable *os = owner_solvable(pool, owner);

                if (os && still_depended_on(pool, os, overwriter))
                    aept_log_warning("not disappearing '%s', it is still depended on", owner);
                else
                    aept_do_disappear(ctx, owner, overwriter_name, overwriter_version, owners);
            }
        }

        aept_fileset_free(&drop);
        free(list_path);
        free(tmp_path);
    }
}
