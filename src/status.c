/* status.c - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

int aept_status_load(void)
{
    FILE *fp, *mem;
    char *buf = NULL;
    size_t buf_size = 0;
    char line[4096];
    int r;

    static const char unpacked_status[] = "Status: install ok unpacked";
    static const char installed_status[] = "Status: install ok installed";

    if (!aept_file_exists(aept_cfg->status_file))
        return 0;

    fp = fopen(aept_cfg->status_file, "r");
    if (!fp) {
        aept_log_error("cannot open status file '%s': %s",
                  aept_cfg->status_file, strerror(errno));
        return -1;
    }

    /* Read status file into memory, normalizing "unpacked" to
     * "installed" so libsolv treats all packages as present. */
    mem = open_memstream(&buf, &buf_size);
    if (!mem) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, unpacked_status, sizeof(unpacked_status) - 1) == 0)
            fprintf(mem, "%s\n", installed_status);
        else
            fputs(line, mem);
    }

    fclose(fp);

    if (fflush(mem) != 0 || ferror(mem)) {
        fclose(mem);
        free(buf);
        return -1;
    }
    fclose(mem);

    fp = fmemopen(buf, buf_size, "r");
    if (!fp) {
        free(buf);
        return -1;
    }

    r = aept_solver_load_installed(fp);
    fclose(fp);
    free(buf);

    return r;
}


int aept_status_add(const char *control_path, const char *state)
{
    FILE *src, *old, *dst;
    char *tmp_path = NULL;
    char buf[4096];

    src = fopen(control_path, "r");
    if (!src) {
        aept_log_error("cannot open control file '%s': %s",
                  control_path, strerror(errno));
        return -1;
    }

    aept_asprintf(&tmp_path, "%s.tmp", aept_cfg->status_file);
    dst = fopen(tmp_path, "w");
    if (!dst) {
        aept_log_error("cannot open status file '%s': %s",
                  tmp_path, strerror(errno));
        fclose(src);
        free(tmp_path);
        return -1;
    }

    /* Copy existing status file */
    old = fopen(aept_cfg->status_file, "r");
    if (old) {
        while (fgets(buf, sizeof(buf), old))
            fputs(buf, dst);
        fclose(old);
    }

    /* Append new entry */
    while (fgets(buf, sizeof(buf), src))
        fputs(buf, dst);

    fprintf(dst, "Status: install ok %s\n\n", state);

    fclose(src);

    if (ferror(dst) || fclose(dst) != 0) {
        aept_log_error("failed to write status file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, aept_cfg->status_file) < 0) {
        aept_log_error("cannot rename status file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int aept_status_remove(const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[4096];
    int skip = 0;
    int continuation = 0;

    fp = fopen(aept_cfg->status_file, "r");
    if (!fp)
        return 0;

    aept_asprintf(&tmp_path, "%s.tmp", aept_cfg->status_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        if (!continuation) {
            if (strncmp(buf, "Package: ", 9) == 0) {
                char pkg_name[256];
                sscanf(buf + 9, "%255s", pkg_name);
                skip = (strcmp(pkg_name, name) == 0);
            }

            if (buf[0] == '\n' || buf[0] == '\0')
                skip = 0;
        }

        if (!skip)
            fputs(buf, tmp);

        continuation = aept_fgets_is_truncated(buf, sizeof(buf));
    }

    fclose(fp);

    if (ferror(tmp) || fclose(tmp) != 0) {
        aept_log_error("failed to write status file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, aept_cfg->status_file) < 0) {
        aept_log_error("cannot rename status file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int aept_status_mark_auto(const char *name)
{
    if (aept_status_is_auto(name))
        return 0;

    FILE *fp = fopen(aept_cfg->auto_file, "a");
    if (!fp) {
        aept_log_error("cannot open auto-installed file '%s': %s",
                  aept_cfg->auto_file, strerror(errno));
        return -1;
    }

    fprintf(fp, "%s\n", name);

    if (ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write auto-installed file '%s'",
                  aept_cfg->auto_file);
        return -1;
    }

    return 0;
}

int aept_status_unmark_auto(const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[256];
    int found = 0;

    fp = fopen(aept_cfg->auto_file, "r");
    if (!fp)
        return 0;

    aept_asprintf(&tmp_path, "%s.tmp", aept_cfg->auto_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        sscanf(buf, "%255s", pkg_name);
        if (strcmp(pkg_name, name) == 0) {
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
        aept_log_error("failed to write auto-installed file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, aept_cfg->auto_file) < 0) {
        aept_log_error("cannot rename auto-installed file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int aept_status_is_auto(const char *name)
{
    FILE *fp;
    char buf[256];

    fp = fopen(aept_cfg->auto_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        sscanf(buf, "%255s", pkg_name);
        if (strcmp(pkg_name, name) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

int aept_status_clear_auto(void)
{
    FILE *fp = fopen(aept_cfg->auto_file, "w");
    if (!fp) {
        aept_log_error("cannot open auto-installed file '%s': %s",
                  aept_cfg->auto_file, strerror(errno));
        return -1;
    }
    fclose(fp);
    return 0;
}

int aept_status_load_auto_set(aept_fileset_t *set)
{
    FILE *fp;
    char buf[256];

    fp = fopen(aept_cfg->auto_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        char pkg_name[256];
        if (sscanf(buf, "%255s", pkg_name) == 1)
            aept_fileset_add(set, pkg_name);
    }

    fclose(fp);
    aept_fileset_sort(set);
    return 0;
}
