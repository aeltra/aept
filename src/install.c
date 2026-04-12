/* install.c - install/upgrade orchestration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solvable.h>
#include <solv/transaction.h>

#include "aept/aept.h"
#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/clash.h"
#include "aept/conffile.h"
#include "aept/config.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/owner_index.h"
#include "aept/pin.h"
#include "aept/remove.h"
#include "aept/script.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/trigger.h"
#include "aept/install.h"
#include "aept/util.h"

static int load_repos(struct aept_ctx *ctx)
{
    int i;

    for (i = 0; i < ctx->config.nsources; i++) {
        char *list_path = NULL;
        FILE *fp;

        aept_asprintf(&list_path, "%s/%s", ctx->config.lists_dir, ctx->config.sources[i].name);

        fp = fopen(list_path, "r");
        if (!fp) {
            aept_log_error("cannot open package list '%s': %s\n"
                      "  (have you run 'aept update'?)",
                      list_path, strerror(errno));
            free(list_path);
            return -1;
        }

        int r = aept_solver_load_repo(ctx, ctx->config.sources[i].name, fp, i);
        fclose(fp);
        free(list_path);

        if (r < 0)
            return -1;
    }

    return 0;
}

/* Look up the installed version of a package by name.
 * Returns a pool string pointer (valid until aept_solver_fini), or NULL. */
static const char *installed_version(Pool *pool, const char *name)
{
    Repo *installed = pool->installed;
    Id p;
    Solvable *s;

    if (!installed)
        return NULL;

    FOR_REPO_SOLVABLES(installed, p, s) {
        if (strcmp(pool_id2str(pool, s->name), name) == 0)
            return pool_id2str(pool, s->evr);
    }

    return NULL;
}

/* Check whether name was covered by an INSTALL step in the transaction. */
static int name_in_transaction(const char *name, Transaction *trans, Pool *pool)
{
    int i;

    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_INSTALL)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        if (strcmp(pool_id2str(pool, s->name), name) == 0)
            return 1;
    }

    return 0;
}

static int display_transaction(struct aept_ctx *ctx, Transaction *trans,
                               Pool *pool, const char **ri_names,
                               int ri_count, int user_count)
{
    int i;
    int n_install = 0;
    int n_upgrade = 0;
    int n_erase = 0;
    int n_reinstall = 0;
    int total;

    if ((!trans || trans->steps.count == 0) && ri_count == 0) {
        aept_log_info("nothing to do");
        return 1;
    }

    total = (trans ? trans->steps.count : 0) + ri_count;
    const char **install_names = aept_malloc(total * sizeof(char *));
    const char **upgrade_names = aept_malloc(total * sizeof(char *));
    const char **erase_names = aept_malloc(total * sizeof(char *));
    const char **reinstall_names = aept_malloc(total * sizeof(char *));

    if (trans) {
        for (i = 0; i < trans->steps.count; i++) {
            Id p = trans->steps.elements[i];
            int type = transaction_type(trans, p,
                SOLVER_TRANSACTION_SHOW_ACTIVE |
                SOLVER_TRANSACTION_SHOW_ALL);

            Solvable *s = pool_id2solvable(pool, p);
            const char *name = pool_id2str(pool, s->name);

            if (type == SOLVER_TRANSACTION_REINSTALL) {
                reinstall_names[n_reinstall++] = name;
            } else if (type == SOLVER_TRANSACTION_UPGRADE ||
                    type == SOLVER_TRANSACTION_DOWNGRADE) {
                upgrade_names[n_upgrade++] = name;
            } else if ((type & 0xf0) == SOLVER_TRANSACTION_INSTALL) {
                install_names[n_install++] = name;
            } else if (type == SOLVER_TRANSACTION_UPGRADED ||
                    type == SOLVER_TRANSACTION_DOWNGRADED ||
                    type == SOLVER_TRANSACTION_REINSTALLED) {
                /* old version being replaced — skip */
            } else if ((type & 0xf0) == SOLVER_TRANSACTION_ERASE) {
                erase_names[n_erase++] = name;
            }
        }
    }

    for (i = 0; i < ri_count; i++) {
        Id avail = aept_solver_find_available(ctx->solver, ri_names[i]);
        if (!avail)
            continue;

        Solvable *s = pool_id2solvable(pool, avail);
        const char *pkg_name = pool_id2str(pool, s->name);

        if (trans && name_in_transaction(pkg_name, trans, pool))
            continue;

        const char *ver = installed_version(pool, pkg_name);
        if (!ver)
            continue;

        reinstall_names[n_reinstall++] = pkg_name;
    }

    aept_transaction_t txn = {
        .install   = install_names,  .n_install   = n_install,
        .upgrade   = upgrade_names,  .n_upgrade   = n_upgrade,
        .reinstall = reinstall_names,.n_reinstall  = n_reinstall,
        .remove    = erase_names,    .n_remove    = n_erase,
    };

    aept_display_transaction(&txn);

    free(install_names);
    free(upgrade_names);
    free(erase_names);
    free(reinstall_names);

    if ((n_erase > 0 ||
                (user_count > 0 &&
                 n_install + n_upgrade > user_count)) &&
            !aept_confirm_continue())
        return -1;

    return 0;
}

static int do_install_package(struct aept_ctx *ctx, const char *ipk_path,
                              Pool *pool, Id p, const char *old_version,
                              aept_owner_index_t *owners)
{
    Solvable *s = pool_id2solvable(pool, p);
    const char *name = pool_id2str(pool, s->name);
    struct aept_ar *ctrl_ar = NULL;
    struct aept_ar *data_ar = NULL;
    char *tmpdir = NULL;
    if (!aept_pkg_name_is_safe(name)) {
        aept_log_error("refusing to install package with unsafe name '%s'", name);
        return -1;
    }
    char *ctrl_path = NULL;
    char *list_path = NULL;
    int r = -1;

    aept_log_info("installing %s", name);

    aept_asprintf(&tmpdir, "%s/aept-XXXXXX", ctx->config.tmp_dir);

    if (!mkdtemp(tmpdir)) {
        aept_log_error("failed to create temp directory: %s",
                  strerror(errno));
        free(tmpdir);
        return -1;
    }

    /* Extract control archive */
    ctrl_ar = aept_ar_open_pkg_control_archive(ipk_path);
    if (!ctrl_ar) {
        aept_log_error("failed to open control archive in '%s'", ipk_path);
        goto cleanup;
    }

    r = aept_ar_extract_all(ctrl_ar, tmpdir, NULL, NULL, NULL, NULL);
    aept_ar_close(ctrl_ar);
    ctrl_ar = NULL;

    if (r < 0) {
        aept_log_error("failed to extract control archive");
        goto cleanup;
    }

    /* Run preinst */
    r = aept_run_script(ctx, tmpdir, NULL, "preinst",
                   old_version ? "upgrade" : "install", old_version);
    if (r != 0)
        goto cleanup;

    /* Check for file conflicts before extraction */
    r = aept_clash_check(ctx, ipk_path, pool, p, NULL, owners);
    if (r != 0) {
        r = -1;
        goto cleanup;
    }

    /* Extract data archive to root, recording each entry so the
     * .list file can be written without re-opening the archive. */
    aept_ar_file_list_t extracted;
    aept_ar_file_list_init(&extracted);

    data_ar = aept_ar_open_pkg_data_archive(ipk_path, ctx->config.ignore_uid);
    if (!data_ar) {
        aept_log_error("failed to open data archive in '%s'", ipk_path);
        aept_ar_file_list_free(&extracted);
        r = -1;
        goto cleanup;
    }

    char *extract_root = aept_config_root_path(&ctx->config, "/");
    r = aept_ar_extract_all(data_ar, extract_root, NULL, NULL, NULL,
                             &extracted);
    free(extract_root);
    aept_ar_close(data_ar);
    data_ar = NULL;

    if (r < 0) {
        aept_log_error("failed to extract data archive");
        aept_ar_file_list_free(&extracted);
        goto cleanup;
    }

    /* Record file list from the in-memory capture */
    aept_file_mkdir_hier(ctx->config.info_dir, 0755);
    aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, name);

    {
        FILE *list_fp = fopen(list_path, "w");
        if (list_fp) {
            if (aept_ar_file_list_write(&extracted, list_fp) < 0
                    || ferror(list_fp) || fclose(list_fp) != 0)
                aept_log_warning("failed to write file list '%s'", list_path);
        } else {
            aept_log_warning("failed to write file list '%s'", list_path);
        }
    }

    aept_ar_file_list_free(&extracted);

    /* Save conffile metadata */
    {
        aept_conffile_set_t new_cf;
        aept_conffile_set_init(&new_cf);

        if (aept_conffile_parse_list(tmpdir, &new_cf) == 0 && new_cf.count > 0) {
            for (int ci = 0; ci < new_cf.count; ci++) {
                char *cf_path = aept_config_root_path(&ctx->config, new_cf.entries[ci].path);
                char *md5 = aept_conffile_md5(cf_path);
                free(cf_path);
                if (md5) {
                    free(new_cf.entries[ci].md5);
                    new_cf.entries[ci].md5 = md5;
                }
            }
            aept_conffile_save(ctx, name, &new_cf);
        }

        aept_conffile_set_free(&new_cf);
    }

    /* Copy scripts to info_dir */
    const char *scripts[] = {
        "preinst", "postinst", "prerm", "postrm", "trigger", NULL
    };
    for (int i = 0; scripts[i]; i++) {
        char *src = NULL, *dst = NULL;
        aept_asprintf(&src, "%s/%s", tmpdir, scripts[i]);
        if (aept_file_exists(src)) {
            aept_asprintf(&dst, "%s/%s.%s", ctx->config.info_dir, name, scripts[i]);
            if (rename(src, dst) < 0 && aept_file_copy(src, dst) < 0)
                aept_log_warning("failed to install %s script for '%s'",
                            scripts[i], name);
            free(dst);
        }
        free(src);
    }

    /* Copy triggers file to info_dir */
    {
        char *trig_src = NULL, *trig_dst = NULL;
        aept_asprintf(&trig_src, "%s/triggers", tmpdir);
        if (aept_file_exists(trig_src)) {
            aept_asprintf(&trig_dst, "%s/%s.triggers",
                          ctx->config.info_dir, name);
            if (rename(trig_src, trig_dst) < 0
                    && aept_file_copy(trig_src, trig_dst) < 0)
                aept_log_warning("failed to install triggers for '%s'", name);
            free(trig_dst);
        }
        free(trig_src);
    }

    /* Run postinst */
    const char *state = "installed";

    r = aept_run_script(ctx, ctx->config.info_dir, name, "postinst",
                   "configure", old_version);
    if (r != 0) {
        aept_log_error("postinst failed for '%s'", name);
        state = "unpacked";
        r = 0;
    }

    /* Write the .control file with the install state.  This reads the
     * raw control from tmpdir and writes it to info_dir in one step,
     * replacing the separate copy + rewrite that used to happen. */
    {
        char *ctrl_src = NULL;
        aept_asprintf(&ctrl_src, "%s/control", tmpdir);
        aept_asprintf(&ctrl_path, "%s/%s.control", ctx->config.info_dir, name);
        aept_status_add(ctx, ctrl_src, ctrl_path, state);
        free(ctrl_src);
        free(ctrl_path);
        ctrl_path = NULL;
    }

    if (owners && list_path)
        aept_owner_index_add_owner_files(owners, name, list_path);

    aept_log_debug("installed %s", name);
    r = 0;

cleanup:
    free(list_path);

    /* Clean up tmpdir */
    {
        const char *rm_argv[] = {"rm", "-rf", tmpdir, NULL};
        aept_system(rm_argv);
    }

    free(tmpdir);
    return r;
}

static void remove_info_files(struct aept_ctx *ctx, const char *name)
{
    const char *exts[] = {
        "list", "control",
        "preinst", "postinst", "prerm", "postrm",
        "trigger", "triggers", NULL
    };

    for (int i = 0; exts[i]; i++) {
        char *path = NULL;
        aept_asprintf(&path, "%s/%s.%s", ctx->config.info_dir, name, exts[i]);
        unlink(path);
        free(path);
    }
}

static int do_upgrade_package(struct aept_ctx *ctx, const char *ipk_path,
                              Pool *pool, Id p, const char *old_version,
                              const char *new_version,
                              aept_fileset_t *installed_files,
                              aept_owner_index_t *owners)
{
    Solvable *s = pool_id2solvable(pool, p);
    const char *name = pool_id2str(pool, s->name);
    struct aept_ar *ctrl_ar = NULL;
    struct aept_ar *data_ar = NULL;
    char *tmpdir = NULL;
    char *ctrl_path = NULL;
    char *list_path = NULL;
    aept_conffile_set_t old_cf;
    int have_old_cf = 0;
    int is_reinstall = old_version && new_version &&
                       strcmp(old_version, new_version) == 0;
    int r = -1;

    if (!aept_pkg_name_is_safe(name)) {
        aept_log_error("refusing to upgrade package with unsafe name '%s'", name);
        return -1;
    }

    aept_log_info("%s %s", is_reinstall ? "reinstalling" : "upgrading", name);

    aept_ar_file_list_t extracted;
    aept_ar_file_list_init(&extracted);

    aept_asprintf(&tmpdir, "%s/aept-XXXXXX", ctx->config.tmp_dir);

    if (!mkdtemp(tmpdir)) {
        aept_log_error("failed to create temp directory: %s",
                  strerror(errno));
        free(tmpdir);
        tmpdir = NULL;
        goto cleanup;
    }

    /* 1. Extract new control archive */
    ctrl_ar = aept_ar_open_pkg_control_archive(ipk_path);
    if (!ctrl_ar) {
        aept_log_error("failed to open control archive in '%s'", ipk_path);
        goto cleanup;
    }

    r = aept_ar_extract_all(ctrl_ar, tmpdir, NULL, NULL, NULL, NULL);
    aept_ar_close(ctrl_ar);
    ctrl_ar = NULL;

    if (r < 0) {
        aept_log_error("failed to extract control archive");
        goto cleanup;
    }

    /* 2. Run old-prerm */
    r = aept_run_script(ctx, ctx->config.info_dir, name, "prerm", "upgrade", new_version);
    if (r != 0) {
        aept_log_error("prerm failed for '%s', aborting upgrade", name);
        goto cleanup;
    }

    /* 3. Run new-preinst */
    r = aept_run_script(ctx, tmpdir, NULL, "preinst", "upgrade", old_version);
    if (r != 0)
        goto cleanup;

    /* 4. Save old file list before overwriting */
    aept_fileset_t old_files;
    aept_fileset_t new_files;
    int have_old_files = 0;

    aept_fileset_init(&old_files);
    aept_fileset_init(&new_files);

    aept_file_mkdir_hier(ctx->config.info_dir, 0755);
    aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, name);

    {
        FILE *lfp = fopen(list_path, "r");
        if (lfp) {
            char lbuf[4096];
            while (fgets(lbuf, sizeof(lbuf), lfp)) {
                char *tab;
                if (aept_fgets_is_truncated(lbuf, sizeof(lbuf))) {
                    aept_fgets_drain_line(lfp);
                    continue;
                }
                lbuf[strcspn(lbuf, "\n")] = '\0';
                tab = strchr(lbuf, '\t');
                if (tab)
                    *tab = '\0';
                aept_fileset_add(&old_files, lbuf);
            }
            fclose(lfp);
            have_old_files = 1;
        }
    }

    /* 5. Check for file conflicts before extraction.  Drop the old
     * version's claim on its files up-front so entries left behind
     * after the upgrade (files not part of the new version) no longer
     * generate false clashes for later packages in the transaction. */
    if (owners)
        aept_owner_index_drop_owner(owners, name);

    r = aept_clash_check(ctx, ipk_path, pool, p, &old_files, owners);
    if (r != 0) {
        r = -1;
        goto cleanup_filesets;
    }

    /* 5b. Prepare conffile set before extraction */
    {
        aept_conffile_set_t new_cf;

        aept_conffile_set_init(&old_cf);
        aept_conffile_set_init(&new_cf);
        aept_conffile_load(ctx, name, &old_cf);
        have_old_cf = 1;
        aept_conffile_parse_list(tmpdir, &new_cf);

        /* Build fileset for conffile-aware extraction */
        aept_fileset_t cf_paths;
        aept_fileset_init(&cf_paths);
        for (int ci = 0; ci < new_cf.count; ci++)
            aept_fileset_add(&cf_paths, new_cf.entries[ci].path);
        aept_fileset_sort(&cf_paths);

        /* 6. Extract new data archive — conffiles get .aept-new suffix */
        data_ar = aept_ar_open_pkg_data_archive(ipk_path, ctx->config.ignore_uid);
        if (!data_ar) {
            aept_log_error("failed to open data archive in '%s'", ipk_path);
            aept_fileset_free(&cf_paths);
            aept_conffile_set_free(&new_cf);
            r = -1;
            goto cleanup_filesets;
        }

        char *extract_root = aept_config_root_path(&ctx->config, "/");
        r = aept_ar_extract_all(data_ar, extract_root, NULL,
                           cf_paths.count > 0 ? &cf_paths : NULL,
                           ".aept-new", &extracted);
        free(extract_root);
        aept_ar_close(data_ar);
        data_ar = NULL;
        aept_fileset_free(&cf_paths);

        if (r < 0) {
            aept_log_error("failed to extract data archive");
            aept_conffile_set_free(&new_cf);
            goto cleanup_filesets;
        }

        /* Resolve conffile conflicts */
        if (new_cf.count > 0)
            aept_conffile_resolve_upgrade(ctx, name, &old_cf, &new_cf);

        aept_conffile_set_free(&new_cf);
    }

    /* 6. Build new_files fileset from the captured extraction list */
    for (int i = 0; i < extracted.count; i++)
        aept_fileset_add(&new_files, extracted.entries[i].path);

    aept_fileset_sort(&new_files);

    /* 7. Remove old files not in new package */
    if (have_old_files) {
        for (int i = 0; i < old_files.count; i++) {
            char *path = old_files.paths[i];

            /* Skip leading ./ */
            while (path[0] == '.' && path[1] == '/')
                path += 2;
            while (path[0] == '/')
                path++;

            if (path[0] == '\0')
                continue;

            if (!aept_archive_path_is_safe(path))
                continue;

            if (aept_fileset_contains(&new_files, old_files.paths[i]))
                continue;

            if (installed_files && aept_fileset_contains(installed_files,
                                                    old_files.paths[i]))
                continue;

            char *full_path = NULL;
            aept_asprintf(&full_path, "%s/%s",
                      ctx->config.offline_root ? ctx->config.offline_root : "", path);

            /* Skip modified conffiles from old package */
            if (old_cf.count > 0) {
                char *abs_path = NULL;
                aept_asprintf(&abs_path, "/%s", path);
                const char *saved_md5 = aept_conffile_set_lookup(&old_cf,
                                                            abs_path);
                if (saved_md5) {
                    char *cur_md5 = aept_conffile_md5(full_path);
                    if (cur_md5 && strcmp(saved_md5, cur_md5) != 0) {
                        aept_log_info("not removing modified conffile '%s'",
                                 abs_path);
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
                aept_log_debug("cannot remove '%s': %s",
                          full_path, strerror(errno));

            free(full_path);
        }
    }

    /* Add new files to installed_files for cross-package protection */
    if (installed_files) {
        for (int i = 0; i < new_files.count; i++)
            aept_fileset_add(installed_files, new_files.paths[i]);
    }

    aept_fileset_free(&new_files);
    aept_fileset_free(&old_files);

    /* 7. Run old-postrm (info_dir still has old scripts) */
    r = aept_run_script(ctx, ctx->config.info_dir, name, "postrm", "upgrade", new_version);
    if (r != 0)
        aept_log_warning("postrm failed for '%s', continuing", name);

    /* 8. Replace info files with new versions */
    remove_info_files(ctx, name);

    const char *scripts[] = {
        "preinst", "postinst", "prerm", "postrm", "trigger", NULL
    };
    for (int i = 0; scripts[i]; i++) {
        char *src = NULL, *dst = NULL;
        aept_asprintf(&src, "%s/%s", tmpdir, scripts[i]);
        if (aept_file_exists(src)) {
            aept_asprintf(&dst, "%s/%s.%s", ctx->config.info_dir, name, scripts[i]);
            if (rename(src, dst) < 0 && aept_file_copy(src, dst) < 0)
                aept_log_warning("failed to install %s script for '%s'",
                            scripts[i], name);
            free(dst);
        }
        free(src);
    }

    /* Copy triggers file to info_dir */
    {
        char *trig_src = NULL, *trig_dst = NULL;
        aept_asprintf(&trig_src, "%s/triggers", tmpdir);
        if (aept_file_exists(trig_src)) {
            aept_asprintf(&trig_dst, "%s/%s.triggers",
                          ctx->config.info_dir, name);
            if (rename(trig_src, trig_dst) < 0
                    && aept_file_copy(trig_src, trig_dst) < 0)
                aept_log_warning("failed to install triggers for '%s'", name);
            free(trig_dst);
        }
        free(trig_src);
    }

    /* Record file list from the in-memory capture (remove_info_files
     * deleted the earlier copy written before the filesets were freed). */
    {
        FILE *list_fp = fopen(list_path, "w");
        if (list_fp) {
            if (aept_ar_file_list_write(&extracted, list_fp) < 0
                    || ferror(list_fp) || fclose(list_fp) != 0)
                aept_log_warning("failed to write file list '%s'", list_path);
        } else {
            aept_log_warning("failed to write file list '%s'", list_path);
        }
    }

    /* 9. Run new-postinst */
    const char *state = "installed";

    r = aept_run_script(ctx, ctx->config.info_dir, name, "postinst", "configure", old_version);
    if (r != 0) {
        aept_log_error("postinst failed for '%s'", name);
        state = "unpacked";
        r = 0;
    }

    /* 10. Write the .control file with the install state */
    {
        char *ctrl_src = NULL;
        aept_asprintf(&ctrl_src, "%s/control", tmpdir);
        aept_asprintf(&ctrl_path, "%s/%s.control", ctx->config.info_dir, name);
        aept_status_add(ctx, ctrl_src, ctrl_path, state);
        free(ctrl_src);
        free(ctrl_path);
        ctrl_path = NULL;
    }

    if (owners)
        aept_owner_index_add_owner_files(owners, name, list_path);

    aept_log_debug("%s %s", is_reinstall ? "reinstalled" : "upgraded", name);
    r = 0;
    goto cleanup;

cleanup_filesets:
    aept_fileset_free(&new_files);
    aept_fileset_free(&old_files);

cleanup:
    aept_ar_file_list_free(&extracted);
    if (have_old_cf)
        aept_conffile_set_free(&old_cf);
    free(list_path);

    if (tmpdir) {
        const char *rm_argv[] = {"rm", "-rf", tmpdir, NULL};
        aept_system(rm_argv);
        free(tmpdir);
    }

    return r;
}

static int do_reinstall(struct aept_ctx *ctx, const char **names, int count,
                        Transaction *trans, Pool *pool,
                        aept_owner_index_t *owners)
{
    int i, r = 0;

    for (i = 0; i < count; i++) {
        Id avail = aept_solver_find_available(ctx->solver, names[i]);
        if (!avail) {
            aept_log_warning("'%s' not found in any repository, "
                        "skipping reinstall", names[i]);
            continue;
        }

        Solvable *s = pool_id2solvable(pool, avail);
        const char *pkg_name = pool_id2str(pool, s->name);

        if (name_in_transaction(pkg_name, trans, pool))
            continue;

        const char *old_ver = installed_version(pool, pkg_name);
        if (!old_ver) {
            aept_log_warning("'%s' is not installed, skipping reinstall",
                        names[i]);
            continue;
        }

        char *ipk_path = NULL;
        int is_local = aept_solver_is_commandline(ctx->solver, avail);

        if (is_local) {
            ipk_path = aept_strdup(aept_solver_commandline_path(ctx->solver, avail));
        } else {
            r = aept_download_package(ctx, avail, pool, &ipk_path);
            if (r < 0)
                return r;
        }

        r = do_upgrade_package(ctx, ipk_path, pool, avail, old_ver, old_ver,
                                NULL, owners);
        if (ctx->config.no_cache && !is_local)
            unlink(ipk_path);
        free(ipk_path);
        if (r < 0)
            return r;
    }

    return 0;
}

int aept_op_install(struct aept_ctx *ctx, const char **names, int name_count,
                 const char **local_paths, int local_count)
{
    Transaction *trans;
    Pool *pool;
    Id *local_ids = NULL;
    int i, r;

    r = aept_solver_init(ctx);
    if (r < 0)
        return -1;

    r = aept_status_load(ctx);
    if (r < 0)
        goto out;

    r = load_repos(ctx);
    if (r < 0)
        goto out;

    aept_pin_load_into_solver(ctx);

    pool = aept_solver_pool(ctx->solver);

    /* Load local .aep files into the solver.  Skip packages that are
     * already installed at the same version (unless --reinstall). */
    int n_local_ids = 0;
    if (local_count > 0) {
        local_ids = aept_malloc(local_count * sizeof(Id));
        for (i = 0; i < local_count; i++) {
            Id lid = aept_solver_load_local(ctx, local_paths[i]);
            if (!lid) {
                free(local_ids);
                local_ids = NULL;
                r = -1;
                goto out;
            }

            Solvable *s = pool_id2solvable(pool, lid);
            const char *pkg_name = pool_id2str(pool, s->name);
            const char *pkg_ver = pool_id2str(pool, s->evr);
            const char *inst_ver = installed_version(pool, pkg_name);

            if (inst_ver && strcmp(inst_ver, pkg_ver) == 0 &&
                    !ctx->config.reinstall) {
                aept_log_info("%s is already installed at version %s",
                         pkg_name, pkg_ver);
                continue;
            }

            local_ids[n_local_ids++] = lid;
        }
    }

    r = aept_solver_resolve_install(ctx, names, name_count, local_ids, n_local_ids);
    if (r < 0)
        goto out;

    trans = aept_solver_transaction(ctx->solver);

    /* Explicitly named packages become manually installed.
     * Resolve through provides so that e.g. "python" correctly
     * unmarks "python3.9" when python3.9 provides python. */
    if (names && !ctx->config.noaction) {
        for (i = 0; i < name_count; i++) {
            aept_status_unmark_auto(ctx, names[i]);
            Id nameid = pool_str2id(pool, names[i], 0);
            if (nameid) {
                Id p2, pp2;
                FOR_PROVIDES(p2, pp2, nameid) {
                    Solvable *s2 = pool_id2solvable(pool, p2);
                    if (s2->repo == pool->installed)
                        aept_status_unmark_auto(ctx, pool_id2str(pool, s2->name));
                }
            }
        }
    }

    /* Local packages are also explicitly requested */
    if (local_ids && !ctx->config.noaction) {
        for (i = 0; i < n_local_ids; i++) {
            Solvable *s = pool_id2solvable(pool, local_ids[i]);
            aept_status_unmark_auto(ctx, pool_id2str(pool, s->name));
        }
    }

    if (display_transaction(ctx, trans, pool,
                           ctx->config.reinstall ? names : NULL,
                           ctx->config.reinstall ? name_count : 0,
                           (names ? name_count : 0) + n_local_ids)) {
        r = 0;
        goto out;
    }

    for (i = 0; i < ctx->config.nsources; i++) {
        if (strncmp(ctx->config.sources[i].url, "https://", 8) != 0)
            aept_log_warning("source '%s' uses insecure transport",
                        ctx->config.sources[i].name);
    }

    if (ctx->config.noaction) {
        aept_log_info("dry run, not installing");
        r = 0;
        goto out;
    }

    if (ctx->config.no_cache && ctx->config.download_only) {
        aept_log_warning("--no-cache ignored with --download-only");
        ctx->config.no_cache = 0;
    }

    /* Download phase */
    char **ipk_paths = NULL;
    ipk_paths = aept_malloc(trans->steps.count * sizeof(char *));
    memset(ipk_paths, 0, trans->steps.count * sizeof(char *));

    if (!ctx->config.no_cache) {
        for (i = 0; i < trans->steps.count; i++) {
            if (aept_cancelled()) {
                aept_log_warning("interrupted, stopping");
                r = -1;
                goto download_cleanup;
            }

            Id p = trans->steps.elements[i];
            int type = transaction_type(trans, p,
                SOLVER_TRANSACTION_SHOW_ACTIVE |
                SOLVER_TRANSACTION_SHOW_ALL);

            if ((type & 0xf0) != SOLVER_TRANSACTION_INSTALL)
                continue;

            if (aept_solver_is_commandline(ctx->solver, p)) {
                ipk_paths[i] = aept_strdup(aept_solver_commandline_path(ctx->solver, p));
                continue;
            }

            r = aept_download_package(ctx, p, pool, &ipk_paths[i]);
            if (r < 0)
                goto download_cleanup;

            if (ctx->config.download_only)
                continue;
        }

        if (ctx->config.download_only) {
            aept_log_info("download complete");
            r = 0;
            goto download_cleanup;
        }
    }

    /* Execute transaction — track installed files so that removals
     * later in the same transaction don't delete them. */
    aept_fileset_t installed_files;
    int fileset_sorted = 0;
    aept_fileset_init(&installed_files);

    /* Build the file→owner index once, up-front.  Replaces the
     * per-file directory scan that aept_clash_check used to do and
     * is the dominant speedup for large transactions. */
    aept_owner_index_t owner_idx;
    aept_owner_index_init(&owner_idx);
    aept_owner_index_build(ctx, &owner_idx);

    aept_trigger_ctx_t tctx;
    aept_trigger_ctx_init(&tctx);

    for (i = 0; i < trans->steps.count; i++) {
        if (aept_cancelled()) {
            aept_log_warning("interrupted, stopping");
            r = -1;
            goto fileset_cleanup;
        }

        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);

        if ((type & 0xf0) == SOLVER_TRANSACTION_ERASE) {
            /* Skip upgrades/downgrades/reinstalls — handled by
             * do_upgrade_package() on the INSTALL side. */
            if (type == SOLVER_TRANSACTION_UPGRADED ||
                    type == SOLVER_TRANSACTION_DOWNGRADED ||
                    type == SOLVER_TRANSACTION_REINSTALLED)
                continue;

            if (!fileset_sorted) {
                aept_fileset_sort(&installed_files);
                fileset_sorted = 1;
            }

            aept_trigger_ctx_collect_dirs(ctx, &tctx, pkg_name);
            r = aept_do_remove(ctx, pkg_name, NULL, &installed_files,
                                &owner_idx);
            if (r < 0 && !ctx->config.force_depends)
                goto fileset_cleanup;
        } else if ((type & 0xf0) == SOLVER_TRANSACTION_INSTALL) {
            if (ctx->config.no_cache) {
                if (aept_solver_is_commandline(ctx->solver, p)) {
                    ipk_paths[i] = aept_strdup(aept_solver_commandline_path(ctx->solver, p));
                } else {
                    r = aept_download_package(ctx, p, pool, &ipk_paths[i]);
                    if (r < 0)
                        goto fileset_cleanup;
                }
            }

            if (!ipk_paths[i])
                continue;

            if (type == SOLVER_TRANSACTION_UPGRADE ||
                    type == SOLVER_TRANSACTION_DOWNGRADE ||
                    type == SOLVER_TRANSACTION_REINSTALL) {
                const char *old_ver = NULL;
                const char *new_ver = pool_id2str(pool, s->evr);
                Id op = transaction_obs_pkg(trans, p);
                if (op) {
                    Solvable *os = pool_id2solvable(pool, op);
                    old_ver = pool_id2str(pool, os->evr);
                }

                if (!fileset_sorted) {
                    aept_fileset_sort(&installed_files);
                    fileset_sorted = 1;
                }

                aept_trigger_ctx_collect_dirs(ctx, &tctx, pkg_name);
                r = do_upgrade_package(ctx, ipk_paths[i], pool, p,
                                       old_ver, new_ver,
                                       &installed_files, &owner_idx);
                fileset_sorted = 0;

                if (r == 0) {
                    aept_trigger_ctx_collect_dirs(ctx, &tctx, pkg_name);
                    aept_trigger_ctx_add_fresh(&tctx, pkg_name);
                }
            } else {
                r = do_install_package(ctx, ipk_paths[i], pool, p, NULL,
                                        &owner_idx);

                if (r == 0) {
                    aept_trigger_ctx_collect_dirs(ctx, &tctx, pkg_name);
                    aept_trigger_ctx_add_fresh(&tctx, pkg_name);
                }
            }

            if (r < 0)
                goto fileset_cleanup;

            /* Mark as auto-installed if this is a fresh install
             * of a dependency (not explicitly requested).
             * Also check provides so that e.g. installing "python"
             * does not auto-mark the providing "python3.9". */
            if ((names || local_ids) &&
                    type != SOLVER_TRANSACTION_UPGRADE &&
                    type != SOLVER_TRANSACTION_DOWNGRADE &&
                    type != SOLVER_TRANSACTION_REINSTALL) {
                int is_explicit = aept_solver_is_commandline(ctx->solver, p);
                int j;
                for (j = 0; !is_explicit && j < name_count; j++) {
                    if (strcmp(names[j], pkg_name) == 0) {
                        is_explicit = 1;
                        break;
                    }
                    Id nameid = pool_str2id(pool, names[j], 0);
                    if (nameid) {
                        Id p2, pp2;
                        FOR_PROVIDES(p2, pp2, nameid) {
                            if (p2 == p) {
                                is_explicit = 1;
                                break;
                            }
                        }
                        if (is_explicit)
                            break;
                    }
                }
                if (!is_explicit)
                    aept_status_mark_auto(ctx, pkg_name);
            }

            /* Record installed files for removal protection.
             * Upgrades/reinstalls already update installed_files
             * in do_upgrade_package(). */
            if (type != SOLVER_TRANSACTION_UPGRADE &&
                    type != SOLVER_TRANSACTION_DOWNGRADE &&
                    type != SOLVER_TRANSACTION_REINSTALL) {
                char *list_path = NULL;
                FILE *lfp;
                char lbuf[4096];

                aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, pkg_name);
                lfp = fopen(list_path, "r");
                free(list_path);

                if (lfp) {
                    while (fgets(lbuf, sizeof(lbuf), lfp)) {
                        char *tab;
                        if (aept_fgets_is_truncated(lbuf, sizeof(lbuf))) {
                            aept_fgets_drain_line(lfp);
                            continue;
                        }
                        lbuf[strcspn(lbuf, "\n")] = '\0';
                        tab = strchr(lbuf, '\t');
                        if (tab)
                            *tab = '\0';
                        aept_fileset_add(&installed_files, lbuf);
                    }
                    fclose(lfp);
                    fileset_sorted = 0;
                }
            }

            if (ctx->config.no_cache) {
                if (!aept_solver_is_commandline(ctx->solver, p))
                    unlink(ipk_paths[i]);
                free(ipk_paths[i]);
                ipk_paths[i] = NULL;
            }
        }
    }

    aept_fileset_free(&installed_files);

    /* Fire triggers for directories modified during the transaction */
    aept_trigger_run_all(ctx, &tctx);
    aept_trigger_ctx_free(&tctx);

    /* Reinstall phase — download and reinstall requested packages
     * that were not covered by the solver transaction. */
    if (ctx->config.reinstall && names) {
        r = do_reinstall(ctx, names, name_count, trans, pool, &owner_idx);
        if (r < 0)
            goto owner_cleanup;
    }

    r = 0;
    goto owner_cleanup;

fileset_cleanup:
    aept_fileset_free(&installed_files);
    aept_trigger_ctx_free(&tctx);

owner_cleanup:
    aept_owner_index_free(&owner_idx);

download_cleanup:
    for (i = 0; i < trans->steps.count; i++)
        free(ipk_paths[i]);
    free(ipk_paths);

out:
    free(local_ids);
    aept_solver_fini(ctx);
    return r;
}
