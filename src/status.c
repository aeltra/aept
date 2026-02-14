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


int status_add(const char *control_path)
{
    FILE *src, *old, *dst;
    char *tmp_path = NULL;
    char buf[4096];

    src = fopen(control_path, "r");
    if (!src) {
        log_error("cannot open control file '%s': %s",
                  control_path, strerror(errno));
        return -1;
    }

    xasprintf(&tmp_path, "%s.tmp", cfg->status_file);
    dst = fopen(tmp_path, "w");
    if (!dst) {
        log_error("cannot open status file '%s': %s",
                  tmp_path, strerror(errno));
        fclose(src);
        free(tmp_path);
        return -1;
    }

    /* Copy existing status file */
    old = fopen(cfg->status_file, "r");
    if (old) {
        while (fgets(buf, sizeof(buf), old))
            fputs(buf, dst);
        fclose(old);
    }

    /* Append new entry */
    while (fgets(buf, sizeof(buf), src))
        fputs(buf, dst);

    fprintf(dst, "Status: install ok installed\n\n");

    fclose(src);

    if (ferror(dst) || fclose(dst) != 0) {
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
