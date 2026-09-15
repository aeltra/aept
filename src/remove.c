/* remove.c - remove orchestration
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
#include <solv/transaction.h>

#include "aept/aept.h"
#include "aept/internal.h"
#include "aept/listfile.h"
#include "aept/conffile.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/owner_index.h"
#include "aept/pin.h"
#include "aept/remove.h"
#include "aept/script.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/trigger.h"
#include "aept/util.h"

/* Sort directories by path length descending (deepest first) */
static int dir_depth_cmp(const void *a, const void *b)
{
    size_t la = strlen(*(const char **)a);
    size_t lb = strlen(*(const char **)b);

    if (lb > la)
        return 1;
    if (lb < la)
        return -1;
    return 0;
}

int aept_remove_files(struct aept_ctx *ctx, const char *name, aept_fileset_t *protected)
{
    char *list_path = NULL;
    aept_list_t l;
    int r;
    aept_conffile_set_t conffiles;
    char **dirs = NULL;
    int n_dirs = 0;
    int dirs_cap = 0;

    aept_conffile_set_init(&conffiles);
    if (!ctx->config.purge)
        aept_conffile_load(ctx, name, &conffiles);

    aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, name);

    r = aept_list_open(&l, list_path);
    free(list_path);

    if (r < 0) {
        aept_conffile_set_free(&conffiles);
        return 0;
    }

    while (aept_list_next(&l)) {
        const char *path = l.entry.stripped;
        unsigned int mode = l.entry.mode;

        if (path[0] == '\0')
            continue;

        if (!aept_archive_path_is_safe(path))
            continue;

        if (protected && aept_fileset_contains(protected, path))
            continue;

        char *full_path = NULL;
        aept_asprintf(&full_path, "%s/%s", ctx->config.offline_root ? ctx->config.offline_root : "",
                      path);

        /* Collect directories for removal after files */
        if (S_ISDIR(mode)) {
            if (n_dirs >= dirs_cap) {
                dirs_cap = dirs_cap ? dirs_cap * 2 : 32;
                dirs = aept_realloc(dirs, dirs_cap * sizeof(char *));
            }
            dirs[n_dirs++] = full_path;
            continue;
        }

        /* Skip modified conffiles unless purging */
        if (conffiles.count > 0) {
            char *abs_path = NULL;
            aept_asprintf(&abs_path, "/%s", path);
            const char *saved_md5 = aept_conffile_set_lookup(&conffiles, abs_path);
            if (saved_md5) {
                char *cur_md5 = aept_conffile_md5(full_path);
                if (cur_md5 && strcmp(saved_md5, cur_md5) != 0) {
                    aept_log_info("not removing modified conffile '%s'", abs_path);
                    free(cur_md5);
                    free(abs_path);
                    free(full_path);
                    continue;
                }
                free(cur_md5);
            }
            free(abs_path);
        }

        if (unlink(full_path) < 0 && errno != ENOENT)
            aept_log_debug("cannot remove '%s': %s", full_path, strerror(errno));

        free(full_path);
    }

    aept_list_close(&l);
    aept_conffile_set_free(&conffiles);

    /* Remove directories deepest-first */
    if (n_dirs > 0) {
        qsort(dirs, n_dirs, sizeof(char *), dir_depth_cmp);

        for (int i = 0; i < n_dirs; i++) {
            if (rmdir(dirs[i]) < 0 && errno != ENOTEMPTY && errno != ENOENT)
                aept_log_debug("cannot rmdir '%s': %s", dirs[i], strerror(errno));
            free(dirs[i]);
        }
        free(dirs);
    }

    return 0;
}

static void remove_info_files(struct aept_ctx *ctx, const char *name)
{
    const char *exts[] = {"list",   "control", "conffiles", "preinst",          "postinst", "prerm",
                          "postrm", "trigger", "triggers",  "triggers-pending", NULL};

    for (int i = 0; exts[i]; i++) {
        char *path = NULL;
        aept_asprintf(&path, "%s/%s.%s", ctx->config.info_dir, name, exts[i]);
        unlink(path);
        free(path);
    }
}

/*
 * A package whose every file has been taken over by another has
 * "disappeared": it owns nothing, so there is nothing left to remove,
 * but it must stop being installed.
 *
 * Deliberately not routed through aept_do_remove(), because three of
 * that function's steps are wrong here:
 *
 *   - no prerm, because nothing knew in advance this would happen;
 *   - no file deletion, because every file it had is now somebody
 *     else's, which is exactly why it disappeared;
 *   - its conffile *records* go with the rest of its info files, while
 *     the conffiles themselves stay on disk -- they were taken over
 *     too, and belong to the overwriting package now.
 *
 * The postrm is told who took over, so it can do its own cleanup.  Its
 * failure is reported and does not stop the disappearance: the files
 * are already gone from this package either way, and leaving it marked
 * installed would describe a state that no longer exists.
 */
int aept_do_disappear(struct aept_ctx *ctx, const char *name, const char *overwriter,
                      const char *overwriter_version, aept_owner_index_t *owners)
{
    const char *args[] = {"disappear", overwriter, overwriter_version, NULL};

    if (!aept_pkg_name_is_safe(name)) {
        aept_log_error("refusing to disappear package with unsafe name '%s'", name);
        return -1;
    }

    aept_log_info("%s disappeared, its files taken over by %s", name, overwriter);

    if (aept_run_script_args(ctx, ctx->config.info_dir, name, "postrm", args) != 0)
        aept_log_warning("postrm failed for disappearing '%s', continuing", name);

    /* Deleting the .control file is what removes the package from the
     * installed-packages database. */
    remove_info_files(ctx, name);

    aept_status_set_mark(ctx, name, AEPT_MARK_MANUAL);
    aept_pin_remove(ctx, name);

    if (owners)
        aept_owner_index_drop_owner(owners, name);

    return 0;
}

int aept_do_remove(struct aept_ctx *ctx, const char *name, const char *new_version,
                   aept_fileset_t *protected, aept_owner_index_t *owners)
{
    int r;

    if (!aept_pkg_name_is_safe(name)) {
        aept_log_error("refusing to remove package with unsafe name '%s'", name);
        return -1;
    }

    aept_log_info("removing %s", name);

    /* Run prerm */
    r = aept_run_script(ctx, ctx->config.info_dir, name, "prerm",
                        new_version ? "upgrade" : "remove", new_version);
    if (r != 0) {
        aept_log_error("prerm failed for '%s', aborting removal", name);
        aept_run_script(ctx, ctx->config.info_dir, name, "postinst", "abort-remove", NULL);
        return -1;
    }

    /* Remove files */
    aept_remove_files(ctx, name, protected);

    /* Run postrm */
    r = aept_run_script(ctx, ctx->config.info_dir, name, "postrm",
                        new_version ? "upgrade" : "remove", new_version);
    if (r != 0)
        aept_log_warning("postrm failed for '%s', continuing", name);

    /* Remove info files.  Deleting the .control file removes the
     * package from the installed-packages database. */
    remove_info_files(ctx, name);

    aept_status_set_mark(ctx, name, AEPT_MARK_MANUAL);
    aept_pin_remove(ctx, name);

    if (owners)
        aept_owner_index_drop_owner(owners, name);

    aept_log_debug("removed %s", name);

    return 0;
}

int aept_op_remove(struct aept_ctx *ctx, const char **names, int count)
{
    Transaction *trans;
    Pool *pool;
    int i, r;

    r = aept_solver_init(ctx);
    if (r < 0)
        return -1;

    r = aept_status_load(ctx);
    if (r < 0)
        goto out;

    /* A protected package named outright is refused here, in words,
     * rather than as the solver problem it would otherwise become.
     * The solver still holds the line for everything not named: a
     * dependant of the request, a conflict.  The name is resolved the
     * way the removal job is, through Provides, so naming a virtual
     * name whose installed provider is protected is refused too. */
    pool = aept_solver_pool(ctx->solver);
    pool_createwhatprovides(pool);
    for (i = 0; i < count; i++) {
        Id nameid = pool_str2id(pool, names[i], 0);
        Id p, pp;

        if (!nameid)
            continue;
        FOR_PROVIDES(p, pp, nameid)
        {
            Solvable *s = pool_id2solvable(pool, p);
            const char *pname = pool_id2str(pool, s->name);

            if (s->repo != pool->installed ||
                aept_status_get_mark(ctx, pname) != AEPT_MARK_PROTECTED)
                continue;
            aept_log_error("'%s' is protected; 'aept mark manual %s' first to remove it", pname,
                           pname);
            r = -1;
            goto out;
        }
    }

    r = aept_solver_resolve_remove(ctx, names, count);
    if (r < 0)
        goto out;

    trans = aept_solver_transaction(ctx->solver);

    if (!trans || trans->steps.count == 0) {
        aept_log_info("nothing to do");
        r = 0;
        goto out;
    }

    int n_erase = 0;
    const char **erase_names = aept_malloc(trans->steps.count * sizeof(char *));

    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
                                    SOLVER_TRANSACTION_SHOW_ACTIVE | SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_ERASE)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        erase_names[n_erase++] = pool_id2str(pool, s->name);
    }

    aept_transaction_t txn = {0};
    txn.remove = erase_names;
    txn.n_remove = n_erase;

    aept_display_transaction(&txn);

    free(erase_names);

    if (n_erase > count && !aept_confirm_continue()) {
        r = 0;
        goto out;
    }

    if (ctx->config.noaction) {
        aept_log_info("dry run, not removing");
        r = 0;
        goto out;
    }

    aept_trigger_ctx_t tctx;
    aept_trigger_ctx_init(&tctx);

    int had_error = 0;

    for (i = 0; i < trans->steps.count; i++) {
        if (aept_cancelled()) {
            aept_log_warning("interrupted, stopping");
            r = -1;
            goto trigger_cleanup;
        }

        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
                                    SOLVER_TRANSACTION_SHOW_ACTIVE | SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_ERASE)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);

        aept_trigger_ctx_collect_dirs(ctx, &tctx, pkg_name);
        r = aept_do_remove(ctx, pkg_name, NULL, NULL, NULL);
        if (r < 0) {
            had_error = 1;
            if (!ctx->config.force_depends && !ctx->config.keep_going)
                goto trigger_cleanup;
        }
    }

    if (aept_trigger_run_all(ctx, &tctx) > 0 && !had_error)
        ctx->last_error = AEPT_ERR_TRIGGER;
    r = had_error ? -1 : 0;

trigger_cleanup:
    aept_trigger_ctx_free(&tctx);

out:
    aept_solver_fini(ctx);
    return r;
}
