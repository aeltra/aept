/* trigger.c - trigger processing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/trigger.h"
#include "aept/util.h"

void aept_trigger_ctx_init(aept_trigger_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void aept_trigger_ctx_free(aept_trigger_ctx_t *ctx)
{
    for (int i = 0; i < ctx->n_dirs; i++)
        free(ctx->dirs[i]);
    free(ctx->dirs);

    for (int i = 0; i < ctx->n_fresh; i++)
        free(ctx->fresh_pkgs[i]);
    free(ctx->fresh_pkgs);

    memset(ctx, 0, sizeof(*ctx));
}

void aept_trigger_ctx_add_dir(aept_trigger_ctx_t *ctx, const char *dir)
{
    /* Normalize: strip leading ./ and / */
    while (dir[0] == '.' && dir[1] == '/')
        dir += 2;
    while (dir[0] == '/')
        dir++;

    if (dir[0] == '\0')
        return;

    /* Deduplicate */
    for (int i = 0; i < ctx->n_dirs; i++) {
        if (strcmp(ctx->dirs[i], dir) == 0)
            return;
    }

    if (ctx->n_dirs >= ctx->dirs_alloc) {
        ctx->dirs_alloc = ctx->dirs_alloc ? ctx->dirs_alloc * 2 : 32;
        ctx->dirs = aept_realloc(ctx->dirs,
                                 ctx->dirs_alloc * sizeof(char *));
    }

    ctx->dirs[ctx->n_dirs++] = aept_strdup(dir);
    ctx->dirs_sorted = 0;
}

void aept_trigger_ctx_add_fresh(aept_trigger_ctx_t *ctx, const char *name)
{
    if (ctx->n_fresh >= ctx->fresh_alloc) {
        ctx->fresh_alloc = ctx->fresh_alloc ? ctx->fresh_alloc * 2 : 8;
        ctx->fresh_pkgs = aept_realloc(ctx->fresh_pkgs,
                                       ctx->fresh_alloc * sizeof(char *));
    }

    ctx->fresh_pkgs[ctx->n_fresh++] = aept_strdup(name);
}

/* Extract parent directory from a path.  Returns a malloc'd string,
 * or NULL if the path has no parent (e.g. "file" with no slash). */
static char *parent_dir(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        return NULL;

    return strndup(path, slash - path);
}

int aept_trigger_ctx_collect_dirs(aept_trigger_ctx_t *ctx, const char *name)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];

    aept_asprintf(&list_path, "%s/%s.list", aept_cfg->info_dir, name);

    fp = fopen(list_path, "r");
    free(list_path);

    if (!fp)
        return 0;

    while (fgets(buf, sizeof(buf), fp)) {
        char *path, *tab;
        unsigned int mode = 0;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }

        buf[strcspn(buf, "\n")] = '\0';

        tab = strchr(buf, '\t');
        if (tab) {
            *tab = '\0';
            mode = (unsigned int)strtoul(tab + 1, NULL, 8);
        }

        path = buf;

        /* Strip leading ./ */
        while (path[0] == '.' && path[1] == '/')
            path += 2;
        while (path[0] == '/')
            path++;

        if (path[0] == '\0')
            continue;

        if (S_ISDIR(mode)) {
            /* Directory entry itself */
            aept_trigger_ctx_add_dir(ctx, path);
        } else {
            /* Regular file: add its parent directory */
            char *dir = parent_dir(path);
            if (dir) {
                aept_trigger_ctx_add_dir(ctx, dir);
                free(dir);
            }
        }
    }

    fclose(fp);
    return 0;
}

static int str_cmp(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

static void sort_and_dedup(char **arr, int *count)
{
    int n = *count;

    if (n <= 1)
        return;

    qsort(arr, n, sizeof(char *), str_cmp);

    int out = 0;
    for (int i = 0; i < n; i++) {
        if (out > 0 && strcmp(arr[out - 1], arr[i]) == 0) {
            free(arr[i]);
        } else {
            arr[out++] = arr[i];
        }
    }

    *count = out;
}

static int is_fresh(aept_trigger_ctx_t *ctx, const char *name)
{
    for (int i = 0; i < ctx->n_fresh; i++) {
        if (strcmp(ctx->fresh_pkgs[i], name) == 0)
            return 1;
    }
    return 0;
}

static int has_glob_chars(const char *s)
{
    for (; *s; s++) {
        if (*s == '*' || *s == '?' || *s == '[')
            return 1;
    }
    return 0;
}

static const char *strip_offline_root(const char *path)
{
    if (!aept_cfg->offline_root)
        return path;

    size_t len = strlen(aept_cfg->offline_root);
    if (strncmp(path, aept_cfg->offline_root, len) == 0)
        return path + len;

    return path;
}

static int run_trigger_script(const char *pkg_name,
                              const char **dirs, int n_dirs)
{
    char *path = NULL;
    int r;

    aept_asprintf(&path, "%s/%s.trigger", aept_cfg->info_dir, pkg_name);

    if (!aept_file_exists(path)) {
        free(path);
        return 0;
    }

    aept_log_info("running trigger for %s", pkg_name);

    const char *run_path = strip_offline_root(path);

    /* Build argv: /bin/sh <script> <dir1> <dir2> ... NULL */
    int argc = 2 + n_dirs;
    const char **argv = aept_malloc((argc + 1) * sizeof(char *));
    argv[0] = "/bin/sh";
    argv[1] = run_path;
    for (int i = 0; i < n_dirs; i++)
        argv[2 + i] = dirs[i];
    argv[argc] = NULL;

    r = aept_system_offline_root(argv);

    free(argv);
    free(path);

    if (r != 0) {
        aept_log_error("trigger script for %s failed with exit code %d",
                       pkg_name, r);
        return r;
    }

    return 0;
}

/* A single entry from triggers-index. */
typedef struct {
    char *pattern;
    char *pkg_name;
    int modify_only;    /* pattern had '+' prefix */
} trigger_entry_t;

int aept_trigger_run_all(aept_trigger_ctx_t *ctx)
{
    char *index_path = NULL;
    FILE *fp;
    char buf[4096];
    trigger_entry_t *entries = NULL;
    int n_entries = 0;
    int entries_alloc = 0;

    if (ctx->n_dirs == 0)
        return 0;

    aept_asprintf(&index_path, "%s/triggers-index", aept_cfg->info_dir);
    fp = fopen(index_path, "r");
    free(index_path);

    if (!fp)
        return 0;

    /* Sort & deduplicate collected directories */
    sort_and_dedup(ctx->dirs, &ctx->n_dirs);

    /* Parse triggers-index */
    while (fgets(buf, sizeof(buf), fp)) {
        char *pattern, *pkg, *tab;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }

        buf[strcspn(buf, "\n")] = '\0';

        if (buf[0] == '\0' || buf[0] == '#')
            continue;

        tab = strchr(buf, '\t');
        if (!tab)
            continue;

        *tab = '\0';
        pattern = buf;
        pkg = tab + 1;

        if (n_entries >= entries_alloc) {
            entries_alloc = entries_alloc ? entries_alloc * 2 : 16;
            entries = aept_realloc(entries,
                                   entries_alloc * sizeof(trigger_entry_t));
        }

        int modify_only = 0;
        if (pattern[0] == '+') {
            modify_only = 1;
            pattern++;
        }

        entries[n_entries].pattern = aept_strdup(pattern);
        entries[n_entries].pkg_name = aept_strdup(pkg);
        entries[n_entries].modify_only = modify_only;
        n_entries++;
    }

    fclose(fp);

    if (n_entries == 0)
        goto cleanup;

    /* Process entries grouped by package.  Since the index is small,
     * a simple O(n*m) approach is fine: for each unique package,
     * collect matching directories across all its patterns. */
    for (int i = 0; i < n_entries; i++) {
        if (!entries[i].pkg_name)
            continue;   /* already processed */

        const char *pkg = entries[i].pkg_name;
        int pkg_is_fresh = is_fresh(ctx, pkg);

        /* Collect matched directories for this package */
        const char **matched = NULL;
        int n_matched = 0;
        int matched_alloc = 0;

        for (int e = i; e < n_entries; e++) {
            if (!entries[e].pkg_name)
                continue;
            if (strcmp(entries[e].pkg_name, pkg) != 0)
                continue;

            const char *pat = entries[e].pattern;

            /* Match against collected directories */
            for (int d = 0; d < ctx->n_dirs; d++) {
                /* Trigger patterns are absolute, dirs are relative */
                char *abs_dir = NULL;
                aept_asprintf(&abs_dir, "/%s", ctx->dirs[d]);

                if (fnmatch(pat, abs_dir, FNM_PATHNAME) == 0) {
                    /* Deduplicate matched dirs */
                    int dup = 0;
                    for (int m = 0; m < n_matched; m++) {
                        if (strcmp(matched[m], abs_dir) == 0) {
                            dup = 1;
                            break;
                        }
                    }
                    if (!dup) {
                        if (n_matched >= matched_alloc) {
                            matched_alloc = matched_alloc
                                ? matched_alloc * 2 : 8;
                            matched = aept_realloc(matched,
                                matched_alloc * sizeof(char *));
                        }
                        matched[n_matched++] = abs_dir;
                        abs_dir = NULL;  /* ownership transferred */
                    }
                }

                free(abs_dir);
            }

            /* For fresh packages with non-modify-only patterns:
             * if the pattern is a concrete path and exists on disk,
             * add it even if it wasn't in ctx->dirs. */
            if (pkg_is_fresh && !entries[e].modify_only
                    && !has_glob_chars(pat)) {
                char *full_path = NULL;
                if (aept_cfg->offline_root)
                    aept_asprintf(&full_path, "%s%s",
                                  aept_cfg->offline_root, pat);
                else
                    full_path = aept_strdup(pat);

                struct stat st;
                if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    int dup = 0;
                    for (int m = 0; m < n_matched; m++) {
                        if (strcmp(matched[m], pat) == 0) {
                            dup = 1;
                            break;
                        }
                    }
                    if (!dup) {
                        if (n_matched >= matched_alloc) {
                            matched_alloc = matched_alloc
                                ? matched_alloc * 2 : 8;
                            matched = aept_realloc(matched,
                                matched_alloc * sizeof(char *));
                        }
                        matched[n_matched++] = aept_strdup(pat);
                    }
                }

                free(full_path);
            }

            /* Mark this entry as consumed so we don't process
             * this package again. */
            if (e != i) {
                free(entries[e].pkg_name);
                entries[e].pkg_name = NULL;
            }
        }

        if (n_matched > 0)
            run_trigger_script(pkg, matched, n_matched);

        for (int m = 0; m < n_matched; m++)
            free((char *)matched[m]);
        free(matched);

        free(entries[i].pkg_name);
        entries[i].pkg_name = NULL;
    }

cleanup:
    for (int i = 0; i < n_entries; i++) {
        free(entries[i].pattern);
        free(entries[i].pkg_name);
    }
    free(entries);

    return 0;
}

int aept_trigger_index_rebuild(void)
{
    DIR *dp;
    struct dirent *de;
    char *index_path = NULL;
    char *tmp_path = NULL;
    FILE *out = NULL;
    int r = -1;

    aept_asprintf(&index_path, "%s/triggers-index", aept_cfg->info_dir);
    aept_asprintf(&tmp_path, "%s/triggers-index.tmp", aept_cfg->info_dir);

    out = fopen(tmp_path, "w");
    if (!out) {
        aept_log_error("cannot create '%s': %s", tmp_path, strerror(errno));
        goto cleanup;
    }

    dp = opendir(aept_cfg->info_dir);
    if (!dp) {
        aept_log_error("cannot open '%s': %s",
                       aept_cfg->info_dir, strerror(errno));
        goto cleanup;
    }

    while ((de = readdir(dp)) != NULL) {
        const char *suffix = ".triggers";
        size_t nlen = strlen(de->d_name);
        size_t slen = strlen(suffix);

        if (nlen <= slen)
            continue;
        if (strcmp(de->d_name + nlen - slen, suffix) != 0)
            continue;

        /* Extract package name */
        char *pkg_name = strndup(de->d_name, nlen - slen);
        if (!pkg_name)
            continue;

        /* Parse the triggers file */
        char *trig_path = NULL;
        aept_asprintf(&trig_path, "%s/%s", aept_cfg->info_dir, de->d_name);

        FILE *tfp = fopen(trig_path, "r");
        free(trig_path);

        if (!tfp) {
            free(pkg_name);
            continue;
        }

        char line[1024];
        while (fgets(line, sizeof(line), tfp)) {
            line[strcspn(line, "\n")] = '\0';

            /* Skip empty lines and comments */
            const char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '\0' || *p == '#')
                continue;

            fprintf(out, "%s\t%s\n", p, pkg_name);
        }

        fclose(tfp);
        free(pkg_name);
    }

    closedir(dp);

    if (fflush(out) != 0 || ferror(out)) {
        aept_log_error("write error on '%s'", tmp_path);
        goto cleanup;
    }

    fclose(out);
    out = NULL;

    if (rename(tmp_path, index_path) < 0) {
        aept_log_error("cannot rename '%s' to '%s': %s",
                       tmp_path, index_path, strerror(errno));
        goto cleanup;
    }

    r = 0;

cleanup:
    if (out)
        fclose(out);
    unlink(tmp_path);
    free(tmp_path);
    free(index_path);
    return r;
}
