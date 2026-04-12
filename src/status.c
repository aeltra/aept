/* status.c - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/util.h"

static const char unpacked_status[] = "Status: install ok unpacked";
static const char installed_status[] = "Status: install ok installed";

/* Read a file into a malloc'd NUL-terminated buffer.  Returns NULL on
 * error.  Sets *out_len to the content length on success. */
static char *slurp_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    struct stat st;
    if (fstat(fileno(fp), &st) < 0) {
        fclose(fp);
        return NULL;
    }

    size_t len = (size_t)st.st_size;
    char *buf = aept_malloc(len + 1);
    size_t got = fread(buf, 1, len, fp);
    fclose(fp);

    if (got != len) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    if (out_len)
        *out_len = len;
    return buf;
}

int aept_status_load(struct aept_ctx *ctx)
{
    DIR *dir;
    struct dirent *ent;
    char *buf = NULL;
    size_t buf_size = 0;
    FILE *mem;
    int r = 0;

    dir = opendir(ctx->config.info_dir);
    if (!dir)
        return 0;

    mem = open_memstream(&buf, &buf_size);
    if (!mem) {
        closedir(dir);
        return -1;
    }

    while ((ent = readdir(dir)) != NULL) {
        const char *dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".control") != 0)
            continue;

        char *path = NULL;
        aept_asprintf(&path, "%s/%s", ctx->config.info_dir, ent->d_name);

        size_t len = 0;
        char *content = slurp_file(path, &len);
        free(path);

        if (!content)
            continue;

        /* Write the control stanza to the feed, normalizing
         * "unpacked" to "installed" for libsolv and adding a
         * default Status line if the file lacks one (pre-migration
         * .control files written by an older aept). */
        const char *p = content;
        int found_status = 0;

        while (*p) {
            const char *eol = strchr(p, '\n');
            size_t llen = eol ? (size_t)(eol - p) : strlen(p);

            if (llen == sizeof(unpacked_status) - 1 &&
                    strncmp(p, unpacked_status,
                            sizeof(unpacked_status) - 1) == 0) {
                fputs(installed_status, mem);
                found_status = 1;
            } else {
                fwrite(p, 1, llen, mem);
                if (llen >= 7 && strncmp(p, "Status:", 7) == 0)
                    found_status = 1;
            }

            if (!eol)
                break;
            fputc('\n', mem);
            p = eol + 1;
        }

        if (!found_status)
            fprintf(mem, "%s\n", installed_status);

        /* Blank-line stanza separator */
        fputc('\n', mem);

        free(content);
    }

    closedir(dir);

    if (fflush(mem) != 0 || ferror(mem)) {
        fclose(mem);
        free(buf);
        return -1;
    }
    fclose(mem);

    if (buf_size > 0) {
        FILE *fp = fmemopen(buf, buf_size, "r");
        if (!fp) {
            free(buf);
            return -1;
        }
        r = aept_solver_load_installed(ctx, fp);
        fclose(fp);
    }
    free(buf);
    return r;
}

int aept_status_add(struct aept_ctx *ctx, const char *control_src,
                    const char *dest_path, const char *state)
{
    (void)ctx;

    size_t ctrl_len = 0;
    char *ctrl = slurp_file(control_src, &ctrl_len);
    if (!ctrl) {
        aept_log_error("cannot read control file '%s': %s",
                  control_src, strerror(errno));
        return -1;
    }

    /* Trim trailing whitespace so the Status line lands cleanly. */
    while (ctrl_len > 0 &&
           (ctrl[ctrl_len - 1] == '\n' || ctrl[ctrl_len - 1] == '\r' ||
            ctrl[ctrl_len - 1] == ' '  || ctrl[ctrl_len - 1] == '\t'))
        ctrl_len--;
    ctrl[ctrl_len] = '\0';

    char *stanza = NULL;
    aept_asprintf(&stanza, "%s\nStatus: install ok %s\n", ctrl, state);
    free(ctrl);

    char *tmp_path = NULL;
    aept_asprintf(&tmp_path, "%s.tmp", dest_path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        aept_log_error("cannot write '%s': %s", tmp_path, strerror(errno));
        free(tmp_path);
        free(stanza);
        return -1;
    }

    int r = -1;
    if (fputs(stanza, fp) == EOF || ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write '%s'", tmp_path);
        unlink(tmp_path);
    } else if (rename(tmp_path, dest_path) < 0) {
        aept_log_error("cannot rename '%s': %s", tmp_path, strerror(errno));
        unlink(tmp_path);
    } else {
        r = 0;
    }

    free(tmp_path);
    free(stanza);
    return r;
}

int aept_status_mark_auto(struct aept_ctx *ctx, const char *name)
{
    if (aept_status_is_auto(ctx, name))
        return 0;

    FILE *fp = fopen(ctx->config.auto_file, "a");
    if (!fp) {
        aept_log_error("cannot open auto-installed file '%s': %s",
                  ctx->config.auto_file, strerror(errno));
        return -1;
    }

    fprintf(fp, "%s\n", name);

    if (ferror(fp) || fclose(fp) != 0) {
        aept_log_error("failed to write auto-installed file '%s'",
                  ctx->config.auto_file);
        return -1;
    }

    return 0;
}

int aept_status_unmark_auto(struct aept_ctx *ctx, const char *name)
{
    FILE *fp, *tmp;
    char *tmp_path = NULL;
    char buf[256];
    int found = 0;

    fp = fopen(ctx->config.auto_file, "r");
    if (!fp)
        return 0;

    aept_asprintf(&tmp_path, "%s.tmp", ctx->config.auto_file);
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

    if (rename(tmp_path, ctx->config.auto_file) < 0) {
        aept_log_error("cannot rename auto-installed file: %s", strerror(errno));
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);
    return 0;
}

int aept_status_is_auto(struct aept_ctx *ctx, const char *name)
{
    FILE *fp;
    char buf[256];

    fp = fopen(ctx->config.auto_file, "r");
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

int aept_status_clear_auto(struct aept_ctx *ctx)
{
    FILE *fp = fopen(ctx->config.auto_file, "w");
    if (!fp) {
        aept_log_error("cannot open auto-installed file '%s': %s",
                  ctx->config.auto_file, strerror(errno));
        return -1;
    }
    fclose(fp);
    return 0;
}

int aept_status_load_auto_set(struct aept_ctx *ctx, aept_fileset_t *set)
{
    FILE *fp;
    char buf[256];

    fp = fopen(ctx->config.auto_file, "r");
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
