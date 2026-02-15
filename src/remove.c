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

#include "aept/aept.h"
#include "aept/config.h"
#include "aept/msg.h"
#include "aept/remove.h"
#include "aept/script.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

static int remove_files(const char *name, const aept_fileset_t *protected)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];

    xasprintf(&list_path, "%s/%s.list", cfg->info_dir, name);

    fp = fopen(list_path, "r");
    free(list_path);

    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        char *path;
        char *tab;

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

        if (protected && fileset_contains(protected, path))
            continue;

        char *full_path = NULL;
        xasprintf(&full_path, "%s/%s",
                  cfg->offline_root ? cfg->offline_root : "", path);

        if (unlink(full_path) < 0 && errno != ENOENT)
            log_debug("cannot remove '%s': %s",
                      full_path, strerror(errno));

        free(full_path);
    }

    fclose(fp);

    return 0;
}

static void remove_info_files(const char *name)
{
    const char *exts[] = {
        "list", "control", "preinst", "postinst", "prerm", "postrm", NULL
    };

    for (int i = 0; exts[i]; i++) {
        char *path = NULL;
        xasprintf(&path, "%s/%s.%s", cfg->info_dir, name, exts[i]);
        unlink(path);
        free(path);
    }
}

int aept_do_remove(const char *name, const char *new_version,
                   const aept_fileset_t *protected)
{
    char *script_args = NULL;
    int r;

    if (!pkg_name_is_safe(name)) {
        log_error("refusing to remove package with unsafe name '%s'", name);
        return -1;
    }

    if (new_version)
        xasprintf(&script_args, "upgrade %s", new_version);

    log_info("removing %s", name);

    /* Run prerm */
    r = run_script(cfg->info_dir, name, "prerm",
                   script_args ? script_args : "remove");
    if (r != 0)
        log_warning("prerm failed for '%s', continuing", name);

    /* Remove files */
    remove_files(name, protected);

    /* Run postrm */
    r = run_script(cfg->info_dir, name, "postrm",
                   script_args ? script_args : "remove");
    if (r != 0)
        log_warning("postrm failed for '%s', continuing", name);

    free(script_args);

    /* Remove info files */
    remove_info_files(name);

    /* Update status */
    status_remove(name);
    status_unmark_auto(name);

    log_info("removed %s", name);

    return 0;
}

int aept_remove(const char **names, int count)
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

    r = solver_resolve_remove(names, count);
    if (r < 0)
        goto out;

    trans = solver_transaction();
    pool = solver_pool();

    if (!trans || trans->steps.count == 0) {
        log_info("nothing to do");
        r = 0;
        goto out;
    }

    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_ERASE)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);
        const char *evr = pool_id2str(pool, s->evr);

        printf("  remove %s %s\n", pkg_name, evr);
    }

    if (cfg->noaction) {
        log_info("dry run, not removing");
        r = 0;
        goto out;
    }

    for (i = 0; i < trans->steps.count; i++) {
        Id p = trans->steps.elements[i];
        int type = transaction_type(trans, p,
            SOLVER_TRANSACTION_SHOW_ACTIVE |
            SOLVER_TRANSACTION_SHOW_ALL);

        if ((type & 0xf0) != SOLVER_TRANSACTION_ERASE)
            continue;

        Solvable *s = pool_id2solvable(pool, p);
        const char *pkg_name = pool_id2str(pool, s->name);

        r = aept_do_remove(pkg_name, NULL, NULL);
        if (r < 0 && !cfg->force_depends)
            goto out;
    }

    r = 0;

out:
    solver_fini();
    return r;
}
