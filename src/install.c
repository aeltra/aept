/* install.c - install/upgrade orchestration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <solv/chksum.h>
#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solvable.h>
#include <solv/transaction.h>

#include "aept/aept.h"
#include "aept/archive.h"
#include "aept/conffile.h"
#include "aept/config.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/remove.h"
#include "aept/script.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/install.h"
#include "aept/util.h"

static int load_repos(void)
{
    int i;

    for (i = 0; i < cfg->nsources; i++) {
        char *list_path = NULL;
        FILE *fp;

        xasprintf(&list_path, "%s/%s", cfg->lists_dir, cfg->sources[i].name);

        fp = fopen(list_path, "r");
        if (!fp) {
            log_error("cannot open package list '%s': %s\n"
                      "  (have you run 'aept update'?)",
                      list_path, strerror(errno));
            free(list_path);
            return -1;
        }

        int r = solver_load_repo(cfg->sources[i].name, fp, i);
        fclose(fp);
        free(list_path);

        if (r < 0)
            return -1;
    }

    return 0;
}

static int display_transaction(Transaction *trans, Pool *pool)
{
    int i;
    int n_install = 0;
    int n_erase = 0;

    printf("Actions:\n");
    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        Solvable *s = pool_id2solvable(pool, p);
        const char *name = pool_id2str(pool, s->name);
        const char *evr = pool_id2str(pool, s->evr);

        if ((type & 0xf0) == SOLVER_TRANSACTION_INSTALL) {
            printf("  install %s %s\n", name, evr);
            n_install++;
        } else if ((type & 0xf0) == SOLVER_TRANSACTION_ERASE) {
            printf("  remove  %s %s\n", name, evr);
            n_erase++;
        }
    }

    if (n_install == 0 && n_erase == 0) {
        log_info("nothing to do");
        return 1;
    }

    printf("Summary:\n  %d to install, %d to remove\n", n_install, n_erase);

    return 0;
}

static int verify_checksum(const char *path, Pool *pool, Solvable *s)
{
    Id checksum_type;
    const unsigned char *expected;
    Chksum *chk;
    FILE *fp;
    char buf[4096];
    size_t n;
    const unsigned char *computed;
    int len;
    const char *name = pool_id2str(pool, s->name);

    expected = solvable_lookup_bin_checksum(s, SOLVABLE_CHECKSUM,
                                            &checksum_type);
    if (!expected) {
        log_warning("no checksum for '%s', skipping verification", name);
        return 0;
    }

    chk = solv_chksum_create(checksum_type);
    if (!chk) {
        log_error("unsupported checksum type for '%s'", name);
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        log_error("cannot open '%s' for checksum verification: %s",
                  path, strerror(errno));
        solv_chksum_free(chk, NULL);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        solv_chksum_add(chk, buf, (int)n);

    fclose(fp);

    computed = solv_chksum_get(chk, &len);

    if (len != solv_chksum_len(checksum_type) ||
            memcmp(computed, expected, len) != 0) {
        log_error("%s checksum mismatch for '%s'",
                  solv_chksum_type2str(checksum_type), name);
        solv_chksum_free(chk, NULL);
        unlink(path);
        return -1;
    }

    solv_chksum_free(chk, NULL);
    return 0;
}

static int download_package(Id p, Pool *pool, char **dest_out)
{
    Solvable *s = pool_id2solvable(pool, p);
    unsigned int medianr;
    const char *location = solvable_lookup_location(s, &medianr);
    int src_idx;
    char *url = NULL;
    char *dest = NULL;
    char *location_copy = NULL;
    char *base;
    int r;

    if (!location) {
        log_error("no download location for '%s'",
                  pool_id2str(pool, s->name));
        return -1;
    }

    src_idx = solver_solvable_source_index(p);
    if (src_idx < 0 || src_idx >= cfg->nsources) {
        log_error("unknown source for '%s'",
                  pool_id2str(pool, s->name));
        return -1;
    }

    xasprintf(&url, "%s/%s", cfg->sources[src_idx].url, location);

    location_copy = xstrdup(location);
    base = basename(location_copy);
    xasprintf(&dest, "%s/%s", cfg->cache_dir, base);
    free(location_copy);

    file_mkdir_hier(cfg->cache_dir, 0755);

    /* Try cached copy first */
    if (access(dest, F_OK) == 0) {
        if (verify_checksum(dest, pool, s) == 0) {
            log_info("using cached %s",
                     pool_id2str(pool, s->name));
            free(url);
            *dest_out = dest;
            return 0;
        }
        /* checksum failed — verify_checksum already deleted the file */
    }

    r = aept_download(url, dest);
    free(url);

    if (r < 0) {
        free(dest);
        return -1;
    }

    r = verify_checksum(dest, pool, s);
    if (r < 0) {
        free(dest);
        return -1;
    }

    *dest_out = dest;
    return 0;
}

static int do_install_package(const char *ipk_path, Pool *pool, Id p,
                              const char *old_version)
{
    Solvable *s = pool_id2solvable(pool, p);
    const char *name = pool_id2str(pool, s->name);
    struct aept_ar *ctrl_ar = NULL;
    struct aept_ar *data_ar = NULL;
    char *tmpdir = NULL;
    char *preinst_args = NULL;
    char *postinst_args = NULL;

    if (!pkg_name_is_safe(name)) {
        log_error("refusing to install package with unsafe name '%s'", name);
        return -1;
    }
    char *ctrl_path = NULL;
    char *list_path = NULL;
    int r = -1;

    if (old_version) {
        xasprintf(&preinst_args, "upgrade %s", old_version);
        xasprintf(&postinst_args, "configure %s", old_version);
    }

    log_info("installing %s", name);

    xasprintf(&tmpdir, "%s/aept-XXXXXX", cfg->tmp_dir);

    if (!mkdtemp(tmpdir)) {
        log_error("failed to create temp directory: %s",
                  strerror(errno));
        free(tmpdir);
        return -1;
    }

    /* Extract control archive */
    ctrl_ar = ar_open_pkg_control_archive(ipk_path);
    if (!ctrl_ar) {
        log_error("failed to open control archive in '%s'", ipk_path);
        goto cleanup;
    }

    r = ar_extract_all(ctrl_ar, tmpdir, NULL);
    ar_close(ctrl_ar);
    ctrl_ar = NULL;

    if (r < 0) {
        log_error("failed to extract control archive");
        goto cleanup;
    }

    /* Run preinst */
    r = run_script(tmpdir, NULL, "preinst",
                   preinst_args ? preinst_args : "install");
    if (r != 0)
        goto cleanup;

    /* Extract data archive to root */
    data_ar = ar_open_pkg_data_archive(ipk_path);
    if (!data_ar) {
        log_error("failed to open data archive in '%s'", ipk_path);
        r = -1;
        goto cleanup;
    }

    char *extract_root = config_root_path("/");
    r = ar_extract_all(data_ar, extract_root, NULL);
    free(extract_root);
    ar_close(data_ar);
    data_ar = NULL;

    if (r < 0) {
        log_error("failed to extract data archive");
        goto cleanup;
    }

    /* Record file list */
    file_mkdir_hier(cfg->info_dir, 0755);
    xasprintf(&list_path, "%s/%s.list", cfg->info_dir, name);

    data_ar = ar_open_pkg_data_archive(ipk_path);
    if (data_ar) {
        FILE *list_fp = fopen(list_path, "w");
        if (list_fp) {
            ar_extract_paths_to_stream(data_ar, list_fp);
            if (ferror(list_fp) || fclose(list_fp) != 0)
                log_warning("failed to write file list '%s'", list_path);
        }
        ar_close(data_ar);
        data_ar = NULL;
    }

    /* Save conffile metadata */
    {
        aept_conffile_set_t new_cf;
        conffile_set_init(&new_cf);

        if (conffile_parse_list(tmpdir, &new_cf) == 0 && new_cf.count > 0) {
            for (int ci = 0; ci < new_cf.count; ci++) {
                char *cf_path = config_root_path(new_cf.entries[ci].path);
                char *md5 = conffile_md5(cf_path);
                free(cf_path);
                if (md5) {
                    free(new_cf.entries[ci].md5);
                    new_cf.entries[ci].md5 = md5;
                }
            }
            conffile_save(name, &new_cf);
        }

        conffile_set_free(&new_cf);
    }

    /* Copy control files to info_dir */
    xasprintf(&ctrl_path, "%s/control", tmpdir);
    if (file_exists(ctrl_path)) {
        char *dest = NULL;
        xasprintf(&dest, "%s/%s.control", cfg->info_dir, name);
        if (rename(ctrl_path, dest) < 0 && file_copy(ctrl_path, dest) < 0)
            log_warning("failed to install control file for '%s'", name);
        free(dest);
    }
    free(ctrl_path);

    /* Copy scripts to info_dir */
    const char *scripts[] = {"preinst", "postinst", "prerm", "postrm", NULL};
    for (int i = 0; scripts[i]; i++) {
        char *src = NULL, *dst = NULL;
        xasprintf(&src, "%s/%s", tmpdir, scripts[i]);
        if (file_exists(src)) {
            xasprintf(&dst, "%s/%s.%s", cfg->info_dir, name, scripts[i]);
            if (rename(src, dst) < 0 && file_copy(src, dst) < 0)
                log_warning("failed to install %s script for '%s'",
                            scripts[i], name);
            free(dst);
        }
        free(src);
    }

    /* Run postinst */
    const char *state = "installed";

    r = run_script(cfg->info_dir, name, "postinst",
                   postinst_args ? postinst_args : "configure");
    if (r != 0) {
        log_error("postinst failed for '%s'", name);
        state = "unpacked";
        r = 0;
    }

    /* Update status */
    ctrl_path = NULL;
    xasprintf(&ctrl_path, "%s/%s.control", cfg->info_dir, name);
    status_remove(name);
    status_add(ctrl_path, state);
    free(ctrl_path);
    ctrl_path = NULL;

    log_info("installed %s", name);
    r = 0;

cleanup:
    free(preinst_args);
    free(postinst_args);
    free(list_path);

    /* Clean up tmpdir */
    {
        const char *rm_argv[] = {"rm", "-rf", tmpdir, NULL};
        xsystem(rm_argv);
    }

    free(tmpdir);
    return r;
}

static void remove_info_files(const char *name)
{
    const char *exts[] = {
        "list", "control", "conffiles",
        "preinst", "postinst", "prerm", "postrm", NULL
    };

    for (int i = 0; exts[i]; i++) {
        char *path = NULL;
        xasprintf(&path, "%s/%s.%s", cfg->info_dir, name, exts[i]);
        unlink(path);
        free(path);
    }
}

static int do_upgrade_package(const char *ipk_path, Pool *pool, Id p,
                              const char *old_version, const char *new_version,
                              aept_fileset_t *installed_files)
{
    Solvable *s = pool_id2solvable(pool, p);
    const char *name = pool_id2str(pool, s->name);
    struct aept_ar *ctrl_ar = NULL;
    struct aept_ar *data_ar = NULL;
    char *tmpdir = NULL;
    char *preinst_args = NULL;
    char *postinst_args = NULL;
    char *prerm_args = NULL;
    char *postrm_args = NULL;
    char *ctrl_path = NULL;
    char *list_path = NULL;
    aept_conffile_set_t old_cf;
    int have_old_cf = 0;
    int r = -1;

    if (!pkg_name_is_safe(name)) {
        log_error("refusing to upgrade package with unsafe name '%s'", name);
        return -1;
    }

    xasprintf(&preinst_args, "upgrade %s", old_version);
    xasprintf(&postinst_args, "configure %s", old_version);
    xasprintf(&prerm_args, "upgrade %s", new_version);
    xasprintf(&postrm_args, "upgrade %s", new_version);

    log_info("upgrading %s", name);

    xasprintf(&tmpdir, "%s/aept-XXXXXX", cfg->tmp_dir);

    if (!mkdtemp(tmpdir)) {
        log_error("failed to create temp directory: %s",
                  strerror(errno));
        free(tmpdir);
        tmpdir = NULL;
        goto cleanup;
    }

    /* 1. Extract new control archive */
    ctrl_ar = ar_open_pkg_control_archive(ipk_path);
    if (!ctrl_ar) {
        log_error("failed to open control archive in '%s'", ipk_path);
        goto cleanup;
    }

    r = ar_extract_all(ctrl_ar, tmpdir, NULL);
    ar_close(ctrl_ar);
    ctrl_ar = NULL;

    if (r < 0) {
        log_error("failed to extract control archive");
        goto cleanup;
    }

    /* 2. Run old-prerm */
    r = run_script(cfg->info_dir, name, "prerm", prerm_args);
    if (r != 0) {
        log_error("prerm failed for '%s', aborting upgrade", name);
        goto cleanup;
    }

    /* 3. Run new-preinst */
    r = run_script(tmpdir, NULL, "preinst", preinst_args);
    if (r != 0)
        goto cleanup;

    /* 4. Save old file list before overwriting */
    aept_fileset_t old_files;
    aept_fileset_t new_files;
    int have_old_files = 0;

    fileset_init(&old_files);
    fileset_init(&new_files);

    file_mkdir_hier(cfg->info_dir, 0755);
    xasprintf(&list_path, "%s/%s.list", cfg->info_dir, name);

    {
        FILE *lfp = fopen(list_path, "r");
        if (lfp) {
            char lbuf[4096];
            while (fgets(lbuf, sizeof(lbuf), lfp)) {
                char *tab;
                lbuf[strcspn(lbuf, "\n")] = '\0';
                tab = strchr(lbuf, '\t');
                if (tab)
                    *tab = '\0';
                fileset_add(&old_files, lbuf);
            }
            fclose(lfp);
            have_old_files = 1;
        }
    }

    /* 5. Extract new data archive (overwrites shared files) */
    data_ar = ar_open_pkg_data_archive(ipk_path);
    if (!data_ar) {
        log_error("failed to open data archive in '%s'", ipk_path);
        r = -1;
        goto cleanup_filesets;
    }

    char *extract_root = config_root_path("/");
    r = ar_extract_all(data_ar, extract_root, NULL);
    free(extract_root);
    ar_close(data_ar);
    data_ar = NULL;

    if (r < 0) {
        log_error("failed to extract data archive");
        goto cleanup_filesets;
    }

    /* 5b. Handle conffile conflicts */
    {
        aept_conffile_set_t new_cf;

        conffile_set_init(&old_cf);
        conffile_set_init(&new_cf);
        conffile_load(name, &old_cf);
        have_old_cf = 1;
        conffile_parse_list(tmpdir, &new_cf);

        if (new_cf.count > 0) {
            char *cf_tmpdir = NULL;
            xasprintf(&cf_tmpdir, "%s/conffiles-new", tmpdir);
            mkdir(cf_tmpdir, 0700);

            aept_fileset_t cf_paths;
            fileset_init(&cf_paths);
            for (int ci = 0; ci < new_cf.count; ci++)
                fileset_add(&cf_paths, new_cf.entries[ci].path);
            fileset_sort(&cf_paths);

            struct aept_ar *cf_ar = ar_open_pkg_data_archive(ipk_path);
            if (cf_ar) {
                ar_extract_selected(cf_ar, &cf_paths, cf_tmpdir);
                ar_close(cf_ar);
            }
            fileset_free(&cf_paths);

            conffile_resolve_upgrade(name, &old_cf, &new_cf, cf_tmpdir);
            free(cf_tmpdir);
        }

        conffile_set_free(&new_cf);
    }

    /* 6. Record new file list */
    data_ar = ar_open_pkg_data_archive(ipk_path);
    if (data_ar) {
        FILE *list_fp = fopen(list_path, "w");
        if (list_fp) {
            ar_extract_paths_to_stream(data_ar, list_fp);
            if (ferror(list_fp) || fclose(list_fp) != 0)
                log_warning("failed to write file list '%s'", list_path);
        }
        ar_close(data_ar);
        data_ar = NULL;

        /* Re-read the list to build new fileset */
        FILE *lfp = fopen(list_path, "r");
        if (lfp) {
            char lbuf[4096];
            while (fgets(lbuf, sizeof(lbuf), lfp)) {
                char *tab;
                lbuf[strcspn(lbuf, "\n")] = '\0';
                tab = strchr(lbuf, '\t');
                if (tab)
                    *tab = '\0';
                fileset_add(&new_files, lbuf);
            }
            fclose(lfp);
        }
    }

    fileset_sort(&new_files);

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

            if (fileset_contains(&new_files, old_files.paths[i]))
                continue;

            if (installed_files && fileset_contains(installed_files,
                                                    old_files.paths[i]))
                continue;

            char *full_path = NULL;
            xasprintf(&full_path, "%s/%s",
                      cfg->offline_root ? cfg->offline_root : "", path);

            /* Skip modified conffiles from old package */
            if (old_cf.count > 0) {
                char *abs_path = NULL;
                xasprintf(&abs_path, "/%s", path);
                const char *saved_md5 = conffile_set_lookup(&old_cf,
                                                            abs_path);
                if (saved_md5) {
                    char *cur_md5 = conffile_md5(full_path);
                    if (cur_md5 && strcmp(saved_md5, cur_md5) != 0) {
                        log_info("not removing modified conffile '%s'",
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
                log_debug("cannot remove '%s': %s",
                          full_path, strerror(errno));

            free(full_path);
        }
    }

    /* Add new files to installed_files for cross-package protection */
    if (installed_files) {
        for (int i = 0; i < new_files.count; i++)
            fileset_add(installed_files, new_files.paths[i]);
    }

    fileset_free(&new_files);
    fileset_free(&old_files);

    /* 7. Run old-postrm (info_dir still has old scripts) */
    r = run_script(cfg->info_dir, name, "postrm", postrm_args);
    if (r != 0)
        log_warning("postrm failed for '%s', continuing", name);

    /* 8. Replace info files with new versions */
    remove_info_files(name);

    xasprintf(&ctrl_path, "%s/control", tmpdir);
    if (file_exists(ctrl_path)) {
        char *dest = NULL;
        xasprintf(&dest, "%s/%s.control", cfg->info_dir, name);
        if (rename(ctrl_path, dest) < 0 && file_copy(ctrl_path, dest) < 0)
            log_warning("failed to install control file for '%s'", name);
        free(dest);
    }
    free(ctrl_path);
    ctrl_path = NULL;

    const char *scripts[] = {"preinst", "postinst", "prerm", "postrm", NULL};
    for (int i = 0; scripts[i]; i++) {
        char *src = NULL, *dst = NULL;
        xasprintf(&src, "%s/%s", tmpdir, scripts[i]);
        if (file_exists(src)) {
            xasprintf(&dst, "%s/%s.%s", cfg->info_dir, name, scripts[i]);
            if (rename(src, dst) < 0 && file_copy(src, dst) < 0)
                log_warning("failed to install %s script for '%s'",
                            scripts[i], name);
            free(dst);
        }
        free(src);
    }

    /* 9. Run new-postinst */
    const char *state = "installed";

    r = run_script(cfg->info_dir, name, "postinst", postinst_args);
    if (r != 0) {
        log_error("postinst failed for '%s'", name);
        state = "unpacked";
        r = 0;
    }

    /* 10. Update status */
    xasprintf(&ctrl_path, "%s/%s.control", cfg->info_dir, name);
    status_remove(name);
    status_add(ctrl_path, state);
    free(ctrl_path);
    ctrl_path = NULL;

    log_info("upgraded %s", name);
    r = 0;
    goto cleanup;

cleanup_filesets:
    fileset_free(&new_files);
    fileset_free(&old_files);

cleanup:
    if (have_old_cf)
        conffile_set_free(&old_cf);
    free(preinst_args);
    free(postinst_args);
    free(prerm_args);
    free(postrm_args);
    free(list_path);

    if (tmpdir) {
        const char *rm_argv[] = {"rm", "-rf", tmpdir, NULL};
        xsystem(rm_argv);
        free(tmpdir);
    }

    return r;
}

/* Look up the installed version of a package by name.
 * Returns a pool string pointer (valid until solver_fini), or NULL. */
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

static int do_reinstall(const char **names, int count,
                        Transaction *trans, Pool *pool)
{
    int i, r = 0;

    for (i = 0; i < count; i++) {
        Id avail = solver_find_available(names[i]);
        if (!avail) {
            log_warning("'%s' not found in any repository, "
                        "skipping reinstall", names[i]);
            continue;
        }

        Solvable *s = pool_id2solvable(pool, avail);
        const char *pkg_name = pool_id2str(pool, s->name);

        if (name_in_transaction(pkg_name, trans, pool))
            continue;

        const char *old_ver = installed_version(pool, pkg_name);
        if (!old_ver) {
            log_warning("'%s' is not installed, skipping reinstall",
                        names[i]);
            continue;
        }

        char *ipk_path = NULL;
        r = download_package(avail, pool, &ipk_path);
        if (r < 0)
            return r;

        r = do_install_package(ipk_path, pool, avail, old_ver);
        if (cfg->no_cache)
            unlink(ipk_path);
        free(ipk_path);
        if (r < 0)
            return r;
    }

    return 0;
}

int aept_install(const char **names, int count)
{
    Transaction *trans;
    Pool *pool;
    int i, r;

    r = solver_init();
    if (r < 0)
        return -1;

    r = status_load();
    if (r < 0)
        goto out;

    r = load_repos();
    if (r < 0)
        goto out;

    for (i = 0; i < cfg->nsources; i++) {
        if (strncmp(cfg->sources[i].url, "https://", 8) != 0)
            log_warning("source '%s' uses insecure transport",
                        cfg->sources[i].name);
    }

    r = solver_resolve_install(names, count);
    if (r < 0)
        goto out;

    trans = solver_transaction();
    pool = solver_pool();

    /* Explicitly named packages become manually installed.
     * Resolve through provides so that e.g. "python" correctly
     * unmarks "python3.9" when python3.9 provides python. */
    if (names && !cfg->noaction) {
        for (i = 0; i < count; i++) {
            status_unmark_auto(names[i]);
            Id nameid = pool_str2id(pool, names[i], 0);
            if (nameid) {
                Id p2, pp2;
                FOR_PROVIDES(p2, pp2, nameid) {
                    Solvable *s2 = pool_id2solvable(pool, p2);
                    if (s2->repo == pool->installed)
                        status_unmark_auto(pool_id2str(pool, s2->name));
                }
            }
        }
    }

    if (!trans || trans->steps.count == 0) {
        if (!cfg->reinstall) {
            log_info("nothing to do");
            r = 0;
            goto out;
        }
    } else {
        if (display_transaction(trans, pool)) {
            if (!cfg->reinstall) {
                r = 0;
                goto out;
            }
        }
    }

    if (cfg->noaction) {
        log_info("dry run, not installing");
        r = 0;
        goto out;
    }

    if (cfg->no_cache && cfg->download_only) {
        log_warning("--no-cache ignored with --download-only");
        cfg->no_cache = 0;
    }

    /* Download phase */
    char **ipk_paths = NULL;
    ipk_paths = xmalloc(trans->steps.count * sizeof(char *));
    memset(ipk_paths, 0, trans->steps.count * sizeof(char *));

    if (!cfg->no_cache) {
        for (i = 0; i < trans->steps.count; i++) {
            Id p = trans->steps.elements[i];
            int type = transaction_type(trans, p,
                SOLVER_TRANSACTION_SHOW_ACTIVE |
                SOLVER_TRANSACTION_SHOW_ALL);

            if ((type & 0xf0) != SOLVER_TRANSACTION_INSTALL)
                continue;

            r = download_package(p, pool, &ipk_paths[i]);
            if (r < 0)
                goto download_cleanup;

            if (cfg->download_only)
                continue;
        }

        if (cfg->download_only) {
            log_info("download complete");
            r = 0;
            goto download_cleanup;
        }
    }

    /* Execute transaction — track installed files so that removals
     * later in the same transaction don't delete them. */
    aept_fileset_t installed_files;
    int fileset_sorted = 0;
    fileset_init(&installed_files);

    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);

        if ((type & 0xf0) == SOLVER_TRANSACTION_ERASE) {
            /* Skip upgrades/downgrades — handled by do_upgrade_package()
             * when the INSTALL side of the transaction is processed. */
            if (type == SOLVER_TRANSACTION_UPGRADED ||
                    type == SOLVER_TRANSACTION_DOWNGRADED)
                continue;

            if (!fileset_sorted) {
                fileset_sort(&installed_files);
                fileset_sorted = 1;
            }

            r = aept_do_remove(pkg_name, NULL, &installed_files);
            if (r < 0 && !cfg->force_depends)
                goto fileset_cleanup;
        } else if ((type & 0xf0) == SOLVER_TRANSACTION_INSTALL) {
            if (cfg->no_cache) {
                r = download_package(p, pool, &ipk_paths[i]);
                if (r < 0)
                    goto fileset_cleanup;
            }

            if (!ipk_paths[i])
                continue;

            if (type == SOLVER_TRANSACTION_UPGRADE ||
                    type == SOLVER_TRANSACTION_DOWNGRADE) {
                const char *old_ver = NULL;
                const char *new_ver = pool_id2str(pool, s->evr);
                Id op = transaction_obs_pkg(trans, p);
                if (op) {
                    Solvable *os = pool_id2solvable(pool, op);
                    old_ver = pool_id2str(pool, os->evr);
                }

                if (!fileset_sorted) {
                    fileset_sort(&installed_files);
                    fileset_sorted = 1;
                }

                r = do_upgrade_package(ipk_paths[i], pool, p,
                                       old_ver, new_ver,
                                       &installed_files);
                fileset_sorted = 0;
            } else {
                r = do_install_package(ipk_paths[i], pool, p, NULL);
            }

            if (r < 0)
                goto fileset_cleanup;

            /* Mark as auto-installed if this is a fresh install
             * of a dependency (not explicitly requested).
             * Also check provides so that e.g. installing "python"
             * does not auto-mark the providing "python3.9". */
            if (names && type != SOLVER_TRANSACTION_UPGRADE &&
                    type != SOLVER_TRANSACTION_DOWNGRADE) {
                int is_explicit = 0;
                int j;
                for (j = 0; j < count; j++) {
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
                    status_mark_auto(pkg_name);
            }

            /* Record installed files for removal protection.
             * Upgrades already update installed_files in
             * do_upgrade_package(). */
            if (type != SOLVER_TRANSACTION_UPGRADE &&
                    type != SOLVER_TRANSACTION_DOWNGRADE) {
                char *list_path = NULL;
                FILE *lfp;
                char lbuf[4096];

                xasprintf(&list_path, "%s/%s.list", cfg->info_dir, pkg_name);
                lfp = fopen(list_path, "r");
                free(list_path);

                if (lfp) {
                    while (fgets(lbuf, sizeof(lbuf), lfp)) {
                        char *tab;
                        lbuf[strcspn(lbuf, "\n")] = '\0';
                        tab = strchr(lbuf, '\t');
                        if (tab)
                            *tab = '\0';
                        fileset_add(&installed_files, lbuf);
                    }
                    fclose(lfp);
                    fileset_sorted = 0;
                }
            }

            if (cfg->no_cache) {
                unlink(ipk_paths[i]);
                free(ipk_paths[i]);
                ipk_paths[i] = NULL;
            }
        }
    }

    fileset_free(&installed_files);

    /* Reinstall phase — download and reinstall requested packages
     * that were not covered by the solver transaction. */
    if (cfg->reinstall && names) {
        r = do_reinstall(names, count, trans, pool);
        if (r < 0)
            goto download_cleanup;
    }

    r = 0;
    goto download_cleanup;

fileset_cleanup:
    fileset_free(&installed_files);

download_cleanup:
    for (i = 0; i < trans->steps.count; i++)
        free(ipk_paths[i]);
    free(ipk_paths);

out:
    solver_fini();
    return r;
}
