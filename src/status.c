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
    FILE *fp, *mem;
    char *buf = NULL;
    size_t buf_size = 0;
    char line[4096];
    int r;

    static const char unpacked_status[] = "Status: install ok unpacked";
    static const char installed_status[] = "Status: install ok installed";

    if (!file_exists(cfg->status_file))
        return 0;

    fp = fopen(cfg->status_file, "r");
    if (!fp) {
        log_error("cannot open status file '%s': %s",
                  cfg->status_file, strerror(errno));
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
    fflush(mem);
    fclose(mem);

    fp = fmemopen(buf, buf_size, "r");
    if (!fp) {
        free(buf);
        return -1;
    }

    r = solver_load_installed(fp);
    fclose(fp);
    free(buf);

    return r;
}


int status_add(const char *control_path, const char *state)
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

    fprintf(dst, "Status: install ok %s\n\n", state);

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

int status_mark_auto(const char *name)
{
    if (status_is_auto(name))
        return 0;

    FILE *fp = fopen(cfg->auto_file, "a");
    if (!fp) {
        log_error("cannot open auto-installed file '%s': %s",
                  cfg->auto_file, strerror(errno));
        return -1;
    }

    fprintf(fp, "%s\n", name);

    if (ferror(fp) || fclose(fp) != 0) {
        log_error("failed to write auto-installed file '%s'",
                  cfg->auto_file);
        return -1;
    }

    return 0;
}

int status_unmark_auto(const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[256];
    int found = 0;

    fp = fopen(cfg->auto_file, "r");
    if (!fp)
        return 0;

    xasprintf(&tmp_path, "%s.tmp", cfg->auto_file);
    tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fclose(fp);
        free(tmp_path);
        return -1;
    }

    while (fgets(buf, sizeof(buf), fp)) {
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
        log_error("failed to write auto-installed file '%s'", tmp_path);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    if (rename(tmp_path, cfg->auto_file) < 0) {
        log_error("cannot rename auto-installed file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int status_is_auto(const char *name)
{
    FILE *fp;
    char buf[256];

    fp = fopen(cfg->auto_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
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

int status_load_auto_set(aept_fileset_t *set)
{
    FILE *fp;
    char buf[256];

    fp = fopen(cfg->auto_file, "r");
    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        char pkg_name[256];
        if (sscanf(buf, "%255s", pkg_name) == 1)
            fileset_add(set, pkg_name);
    }

    fclose(fp);
    fileset_sort(set);
    return 0;
}
