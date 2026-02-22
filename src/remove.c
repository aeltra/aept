/* remove.c - remove orchestration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <solv/pool.h>
#include <solv/solvable.h>
#include <solv/transaction.h>

#include "aept/internal.h"
#include "aept/conffile.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/pin.h"
#include "aept/remove.h"
#include "aept/script.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

int aept_remove_files(const char *name, aept_fileset_t *protected)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];
    aept_conffile_set_t conffiles;

    aept_conffile_set_init(&conffiles);
    if (!aept_cfg->purge)
        aept_conffile_load(name, &conffiles);

    aept_asprintf(&list_path, "%s/%s.list", aept_cfg->info_dir, name);

    fp = fopen(list_path, "r");
    free(list_path);

    if (!fp) {
        aept_conffile_set_free(&conffiles);
        return 0;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        char *path;
        char *tab;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }

        /* Format: path\tmode[\tsymlink_target]\n */
        buf[strcspn(buf, "\n")] = '\0';

        tab = strchr(buf, '\t');
        if (tab)
            *tab = '\0';

        path = buf;

        /* Skip leading ./ */
        while (path[0] == '.' && path[1] == '/')
            path += 2;
        while (path[0] == '/')
            path++;

        if (path[0] == '\0')
            continue;

        if (!aept_archive_path_is_safe(path))
            continue;

        if (protected && aept_fileset_contains(protected, path))
            continue;

        char *full_path = NULL;
        aept_asprintf(&full_path, "%s/%s",
                  aept_cfg->offline_root ? aept_cfg->offline_root : "", path);

        /* Skip modified conffiles unless purging */
        if (conffiles.count > 0) {
            char *abs_path = NULL;
            aept_asprintf(&abs_path, "/%s", path);
            const char *saved_md5 = aept_conffile_set_lookup(&conffiles,
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

    fclose(fp);
    aept_conffile_set_free(&conffiles);

    return 0;
}

static void remove_info_files(const char *name)
{
    const char *exts[] = {
        "list", "control", "conffiles",
        "preinst", "postinst", "prerm", "postrm", NULL
    };

    for (int i = 0; exts[i]; i++) {
        char *path = NULL;
        aept_asprintf(&path, "%s/%s.%s", aept_cfg->info_dir, name, exts[i]);
        unlink(path);
        free(path);
    }
}

int aept_do_remove(const char *name, const char *new_version,
                   aept_fileset_t *protected)
{
    int r;

    if (!aept_pkg_name_is_safe(name)) {
        aept_log_error("refusing to remove package with unsafe name '%s'", name);
        return -1;
    }

    aept_log_info("removing %s", name);

    /* Run prerm */
    r = aept_run_script(aept_cfg->info_dir, name, "prerm",
                   new_version ? "upgrade" : "remove", new_version);
    if (r != 0) {
        aept_log_error("prerm failed for '%s', aborting removal", name);
        return -1;
    }

    /* Remove files */
    aept_remove_files(name, protected);

    /* Run postrm */
    r = aept_run_script(aept_cfg->info_dir, name, "postrm",
                   new_version ? "upgrade" : "remove", new_version);
    if (r != 0)
        aept_log_warning("postrm failed for '%s', continuing", name);

    /* Remove info files */
    remove_info_files(name);

    /* Update status */
    aept_status_remove(name);
    aept_status_unmark_auto(name);
    aept_pin_remove(name);

    aept_log_debug("removed %s", name);

    return 0;
}

int aept_remove(const char **names, int count)
{
    Transaction *trans;
    Pool *pool;
    int i, r;

    r = aept_solver_init();
    if (r < 0)
        return -1;

    r = aept_status_load();
    if (r < 0)
        goto out;

    r = aept_solver_resolve_remove(names, count);
    if (r < 0)
        goto out;

    trans = aept_solver_transaction();
    pool = aept_solver_pool();

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
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_ERASE)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        erase_names[n_erase++] = pool_id2str(pool, s->name);
    }

    if (n_erase > 0) {
        aept_print_heading("The following packages will be REMOVED:");
        aept_print_names(erase_names, n_erase);
    }

    aept_print_heading("0 to install, 0 to upgrade, %d to remove.", n_erase);

    free(erase_names);

    if (n_erase > count && !aept_confirm_continue()) {
        r = 0;
        goto out;
    }

    if (aept_cfg->noaction) {
        aept_log_info("dry run, not removing");
        r = 0;
        goto out;
    }

    for (i = 0; i < trans->steps.count; i++) {
        if (aept_signal_was_interrupted()) {
            aept_log_warning("interrupted, stopping");
            r = -1;
            goto out;
        }

        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_ERASE)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);

        r = aept_do_remove(pkg_name, NULL, NULL);
        if (r < 0 && !aept_cfg->force_depends)
            goto out;
    }

    r = 0;

out:
    aept_solver_fini();
    return r;
}
