/* clash.c - file clash detection
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/solvable.h>

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/clash.h"
#include "aept/msg.h"
#include "aept/owner_index.h"
#include "aept/util.h"

/* Check whether solvable s declares Replaces for owner_name. */
static int solvable_replaces(Pool *pool, Solvable *s, const char *owner_name)
{
    Queue q;
    Id owner_id;
    int i;

    owner_id = pool_str2id(pool, owner_name, 0);
    if (!owner_id)
        return 0;

    queue_init(&q);
    solvable_lookup_deparray(s, SOLVABLE_OBSOLETES, &q, 0);

    for (i = 0; i < q.count; i++) {
        Id dep = q.elements[i];
        Id name;

        if (ISRELDEP(dep)) {
            Reldep *rd = GETRELDEP(pool, dep);
            name = rd->name;
        } else {
            name = dep;
        }

        if (name == owner_id) {
            queue_free(&q);
            return 1;
        }
    }

    queue_free(&q);
    return 0;
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

int aept_clash_check(struct aept_ctx *ctx, const char *ipk_path,
                     Pool *pool, Id p,
                     aept_fileset_t *old_files,
                     aept_owner_index_t *owners)
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

        aept_asprintf(&disk_path, "%s/%s",
                  ctx->config.offline_root ? ctx->config.offline_root : "", stripped);

        if (lstat(disk_path, &st) < 0) {
            free(disk_path);
            continue;
        }

        /* Both are symlinks to the same directory — shared like dirs */
        if (S_ISLNK(st.st_mode) && link_target &&
                same_dir_symlink(disk_path, link_target)) {
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

        /* New package declares Replaces for the owner */
        if (solvable_replaces(pool, s, owner))
            continue;

        aept_log_error("package '%s' wants to install '%s'\n"
                  "  but that file is already provided by package '%s'",
                  pkg_name, stripped, owner);
        clashes++;
    }

    aept_ar_file_list_free(&new_files);
    return clashes;
}
