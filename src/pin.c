/* pin.c - version pinning
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
#include "aept/pin.h"
#include "aept/solver.h"
#include "aept/util.h"

int pin_add(const char *name, const char *version)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[512];
    int replaced = 0;

    xasprintf(&tmp_path, "%s.tmp", cfg->pin_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        log_error("cannot open pin file '%s': %s",
                  tmp_path, strerror(errno));
        free(tmp_path);
        return -1;
    }

    fp = fopen(cfg->pin_file, "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            if (fgets_is_truncated(buf, sizeof(buf))) {
                fgets_drain_line(fp);
                continue;
            }
            char pkg_name[256];
            if (sscanf(buf, "%255s", pkg_name) == 1 &&
                    strcmp(pkg_name, name) == 0) {
                fprintf(tmp, "%s %s\n", name, version);
                replaced = 1;
                continue;
            }
            fputs(buf, tmp);
        }
        fclose(fp);
    }

    if (!replaced)
        fprintf(tmp, "%s %s\n", name, version);

    if (ferror(tmp) || fclose(tmp) != 0) {
        log_error("failed to write pin file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, cfg->pin_file) < 0) {
        log_error("cannot rename pin file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int pin_remove(const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[512];
    int found = 0;

    fp = fopen(cfg->pin_file, "r");
    if (!fp)
        return 0;

    xasprintf(&tmp_path, "%s.tmp", cfg->pin_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        if (fgets_is_truncated(buf, sizeof(buf))) {
            fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        if (sscanf(buf, "%255s", pkg_name) == 1 &&
                strcmp(pkg_name, name) == 0) {
            found = 1;
            continue;
        }
        fputs(buf, tmp);
    }

    fclose(fp);

    if (!found) {
        fclose(tmp);
        unlink(tmp_path);
        free(tmp_path);
        return 0;
    }

    if (ferror(tmp) || fclose(tmp) != 0) {
        log_error("failed to write pin file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, cfg->pin_file) < 0) {
        log_error("cannot rename pin file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

char *pin_lookup(const char *name)
{
    FILE *fp;
    char buf[512];

    fp = fopen(cfg->pin_file, "r");
    if (!fp)
        return NULL;

    while (fgets(buf, sizeof(buf), fp)) {
        if (fgets_is_truncated(buf, sizeof(buf))) {
            fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256], pkg_version[256];
        if (sscanf(buf, "%255s %255s", pkg_name, pkg_version) == 2 &&
                strcmp(pkg_name, name) == 0) {
            fclose(fp);
            return xstrdup(pkg_version);
        }
    }

    fclose(fp);
    return NULL;
}

int pin_load_into_solver(void)
{
    FILE *fp;
    char buf[512];

    fp = fopen(cfg->pin_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        char name[256], version[256];

        if (fgets_is_truncated(buf, sizeof(buf))) {
            fgets_drain_line(fp);
            continue;
        }

        if (sscanf(buf, "%255s %255s", name, version) == 2)
            solver_add_pin(name, version);
    }

    fclose(fp);
    return 0;
}
