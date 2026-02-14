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
#include <unistd.h>

#include <solv/pool.h>
#include <solv/solvable.h>
#include <solv/transaction.h>

#include "aept/aept.h"
#include "aept/archive.h"
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
            aept_msg(AEPT_ERROR, "cannot open package list '%s': %s\n"
                     "  (have you run 'aept update'?)\n",
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

    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        Solvable *s = pool_id2solvable(pool, p);
        const char *name = pool_id2str(pool, s->name);
        const char *evr = pool_id2str(pool, s->evr);

        if ((type & 0xf0) == SOLVER_TRANSACTION_INSTALL) {
            fprintf(stderr, "  install %s %s\n", name, evr);
            n_install++;
        } else if ((type & 0xf0) == SOLVER_TRANSACTION_ERASE) {
            fprintf(stderr, "  remove  %s %s\n", name, evr);
            n_erase++;
        }
    }

    if (n_install == 0 && n_erase == 0) {
        aept_msg(AEPT_NOTICE, "nothing to do\n");
        return 1;
    }

    fprintf(stderr, "\n%d to install, %d to remove\n", n_install, n_erase);

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
        aept_msg(AEPT_ERROR, "no download location for '%s'\n",
                 pool_id2str(pool, s->name));
        return -1;
    }

    src_idx = solver_solvable_source_index(p);
    if (src_idx < 0 || src_idx >= cfg->nsources) {
        aept_msg(AEPT_ERROR, "unknown source for '%s'\n",
                 pool_id2str(pool, s->name));
        return -1;
    }

    xasprintf(&url, "%s/%s", cfg->sources[src_idx].url, location);

    location_copy = xstrdup(location);
    base = basename(location_copy);
    xasprintf(&dest, "%s/%s", cfg->cache_dir, base);
    free(location_copy);

    file_mkdir_hier(cfg->cache_dir, 0755);

    r = aept_download(url, dest);
    free(url);

    if (r < 0) {
        free(dest);
        return -1;
    }

    *dest_out = dest;
    return 0;
}

static int do_install_package(const char *ipk_path, Pool *pool, Id p)
{
    Solvable *s = pool_id2solvable(pool, p);
    const char *name = pool_id2str(pool, s->name);
    struct aept_ar *ctrl_ar = NULL;
    struct aept_ar *data_ar = NULL;
    char tmpdir[] = "/tmp/aept-XXXXXX";
    char *ctrl_path = NULL;
    char *list_path = NULL;
    int r = -1;

    if (!mkdtemp(tmpdir)) {
        aept_msg(AEPT_ERROR, "failed to create temp directory: %s\n",
                 strerror(errno));
        return -1;
    }

    /* Extract control archive */
    ctrl_ar = ar_open_pkg_control_archive(ipk_path);
    if (!ctrl_ar) {
        aept_msg(AEPT_ERROR, "failed to open control archive in '%s'\n",
                 ipk_path);
        goto cleanup;
    }

    r = ar_extract_all(ctrl_ar, tmpdir, NULL);
    ar_close(ctrl_ar);
    ctrl_ar = NULL;

    if (r < 0) {
        aept_msg(AEPT_ERROR, "failed to extract control archive\n");
        goto cleanup;
    }

    /* Run preinst */
    r = run_script(tmpdir, NULL, "preinst", "install");
    if (r != 0)
        goto cleanup;

    /* Extract data archive to root */
    data_ar = ar_open_pkg_data_archive(ipk_path);
    if (!data_ar) {
        aept_msg(AEPT_ERROR, "failed to open data archive in '%s'\n",
                 ipk_path);
        r = -1;
        goto cleanup;
    }

    r = ar_extract_all(data_ar, cfg->root_dir, NULL);
    ar_close(data_ar);
    data_ar = NULL;

    if (r < 0) {
        aept_msg(AEPT_ERROR, "failed to extract data archive\n");
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
            fclose(list_fp);
        }
        ar_close(data_ar);
        data_ar = NULL;
    }

    /* Copy control files to info_dir */
    xasprintf(&ctrl_path, "%s/control", tmpdir);
    if (file_exists(ctrl_path)) {
        char *dest = NULL;
        xasprintf(&dest, "%s/%s.control", cfg->info_dir, name);
        rename(ctrl_path, dest);
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
            rename(src, dst);
            free(dst);
        }
        free(src);
    }

    /* Run postinst */
    r = run_script(cfg->info_dir, name, "postinst", "configure");
    if (r != 0) {
        aept_msg(AEPT_ERROR, "postinst failed for '%s'\n", name);
        /* Continue despite postinst failure — package is installed */
        r = 0;
    }

    /* Update status */
    ctrl_path = NULL;
    xasprintf(&ctrl_path, "%s/%s.control", cfg->info_dir, name);
    status_remove(name);
    status_add(ctrl_path);
    free(ctrl_path);
    ctrl_path = NULL;

    aept_msg(AEPT_NOTICE, "installed %s\n", name);
    r = 0;

cleanup:
    free(list_path);

    /* Clean up tmpdir */
    {
        char *rm_cmd = NULL;
        xasprintf(&rm_cmd, "rm -rf %s", tmpdir);
        const char *rm_argv[] = {"/bin/sh", "-c", rm_cmd, NULL};
        xsystem(rm_argv);
        free(rm_cmd);
    }

    return r;
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

    r = solver_resolve_install(names, count);
    if (r < 0) {
        if (!cfg->force_depends)
            goto out;
        aept_msg(AEPT_NOTICE, "proceeding despite dependency errors "
                 "(--force-depends)\n");
    }

    trans = solver_transaction();
    pool = solver_pool();

    if (!trans || trans->steps.count == 0) {
        aept_msg(AEPT_NOTICE, "nothing to do\n");
        r = 0;
        goto out;
    }

    if (display_transaction(trans, pool)) {
        r = 0;
        goto out;
    }

    if (cfg->noaction) {
        aept_msg(AEPT_NOTICE, "dry run, not installing\n");
        r = 0;
        goto out;
    }

    /* Download phase */
    char **ipk_paths = NULL;
    ipk_paths = xmalloc(trans->steps.count * sizeof(char *));
    memset(ipk_paths, 0, trans->steps.count * sizeof(char *));

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
        aept_msg(AEPT_NOTICE, "download complete\n");
        r = 0;
        goto download_cleanup;
    }

    /* Execute transaction */
    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);

        if ((type & 0xf0) == SOLVER_TRANSACTION_ERASE) {
            r = aept_do_remove(pkg_name);
            if (r < 0 && !cfg->force_depends)
                goto download_cleanup;
        } else if ((type & 0xf0) == SOLVER_TRANSACTION_INSTALL) {
            if (!ipk_paths[i])
                continue;

            r = do_install_package(ipk_paths[i], pool, p);
            if (r < 0)
                goto download_cleanup;
        }
    }

    r = 0;

download_cleanup:
    for (i = 0; i < trans->steps.count; i++)
        free(ipk_paths[i]);
    free(ipk_paths);

out:
    solver_fini();
    return r;
}
