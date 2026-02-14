/* status.c - installed package database
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
#include <solv/repo.h>
#include <solv/solvable.h>
#include <solv/knownid.h>

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

int status_load(void)
{
    FILE *fp;

    if (!file_exists(cfg->status_file))
        return 0;

    fp = fopen(cfg->status_file, "r");
    if (!fp) {
        log_error("cannot open status file '%s': %s",
                  cfg->status_file, strerror(errno));
        return -1;
    }

    int r = solver_load_installed(fp);
    fclose(fp);

    return r;
}

int status_write(void)
{
    Pool *pool = solver_pool();
    FILE *fp;
    char *tmp_path = NULL;
    Id p;
    Solvable *s;

    if (!pool || !pool->installed)
        return 0;

    xasprintf(&tmp_path, "%s.tmp", cfg->status_file);

    fp = fopen(tmp_path, "w");
    if (!fp) {
        log_error("cannot write status file '%s': %s",
                  tmp_path, strerror(errno));
        free(tmp_path);
        return -1;
    }

    int write_err = 0;

    FOR_REPO_SOLVABLES(pool->installed, p, s) {
        const char *name = pool_id2str(pool, s->name);
        const char *evr = pool_id2str(pool, s->evr);
        const char *arch = pool_id2str(pool, s->arch);
        const char *desc = solvable_lookup_str(s, SOLVABLE_DESCRIPTION);

        fprintf(fp, "Package: %s\n", name);
        fprintf(fp, "Version: %s\n", evr);
        if (arch)
            fprintf(fp, "Architecture: %s\n", arch);
        fprintf(fp, "Status: install ok installed\n");

        /* Write dependency fields */
        if (s->requires) {
            Id *reqp, req;
            int first = 1;

            reqp = s->repo->idarraydata + s->requires;
            while ((req = *reqp++) != 0) {
                if (SOLVABLE_PREREQMARKER == req)
                    continue;
                if (first) {
                    fprintf(fp, "Depends: ");
                    first = 0;
                } else {
                    fprintf(fp, ", ");
                }
                fprintf(fp, "%s", pool_dep2str(pool, req));
            }
            if (!first)
                fprintf(fp, "\n");
        }

        if (desc)
            fprintf(fp, "Description: %s\n", desc);

        fprintf(fp, "\n");

        if (ferror(fp)) {
            write_err = 1;
            break;
        }
    }

    if (fclose(fp) != 0)
        write_err = 1;

    if (write_err) {
        log_error("failed to write status file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, cfg->status_file) < 0) {
        log_error("cannot rename status file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int status_add(const char *control_path)
{
    FILE *src, *dst;
    char buf[4096];

    src = fopen(control_path, "r");
    if (!src) {
        log_error("cannot open control file '%s': %s",
                  control_path, strerror(errno));
        return -1;
    }

    dst = fopen(cfg->status_file, "a");
    if (!dst) {
        log_error("cannot open status file '%s': %s",
                  cfg->status_file, strerror(errno));
        fclose(src);
        return -1;
    }

    fprintf(dst, "Status: install ok installed\n");

    while (fgets(buf, sizeof(buf), src))
        fputs(buf, dst);

    fprintf(dst, "\n");

    fclose(src);

    if (ferror(dst) || fclose(dst) != 0) {
        log_error("failed to write status file '%s'",
                  cfg->status_file);
        return -1;
    }

    return 0;
}

int status_remove(const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[4096];
    int skip = 0;

    fp = fopen(cfg->status_file, "r");
    if (!fp)
        return 0;

    xasprintf(&tmp_path, "%s.tmp", cfg->status_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        if (strncmp(buf, "Package: ", 9) == 0) {
            char pkg_name[256];
            sscanf(buf + 9, "%255s", pkg_name);
            skip = (strcmp(pkg_name, name) == 0);
        }

        if (buf[0] == '\n' || buf[0] == '\0')
            skip = 0;

        if (!skip)
            fputs(buf, tmp);
    }

    fclose(fp);

    if (ferror(tmp) || fclose(tmp) != 0) {
        log_error("failed to write status file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, cfg->status_file) < 0) {
        log_error("cannot rename status file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}
