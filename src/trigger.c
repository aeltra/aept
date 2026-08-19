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
#include "aept/status.h"
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
        ctx->dirs = aept_realloc(ctx->dirs, ctx->dirs_alloc * sizeof(char *));
    }

    ctx->dirs[ctx->n_dirs++] = aept_strdup(dir);
    ctx->dirs_sorted = 0;
}

void aept_trigger_ctx_add_fresh(aept_trigger_ctx_t *ctx, const char *name)
{
    if (ctx->n_fresh >= ctx->fresh_alloc) {
        ctx->fresh_alloc = ctx->fresh_alloc ? ctx->fresh_alloc * 2 : 8;
        ctx->fresh_pkgs = aept_realloc(ctx->fresh_pkgs, ctx->fresh_alloc * sizeof(char *));
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

int aept_trigger_ctx_collect_dirs(struct aept_ctx *ctx, aept_trigger_ctx_t *tctx, const char *name)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];

    aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, name);

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
            aept_trigger_ctx_add_dir(tctx, path);
        } else {
            /* Regular file: add its parent directory */
            char *dir = parent_dir(path);
            if (dir) {
                aept_trigger_ctx_add_dir(tctx, dir);
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

static const char *strip_offline_root(struct aept_ctx *ctx, const char *path)
{
    if (!ctx->config.offline_root)
        return path;

    size_t len = strlen(ctx->config.offline_root);
    if (strncmp(path, ctx->config.offline_root, len) == 0)
        return path + len;

    return path;
}

static char *pending_path(struct aept_ctx *ctx, const char *pkg)
{
    char *p = NULL;
    aept_asprintf(&p, "%s/%s.triggers-pending", ctx->config.info_dir, pkg);
    return p;
}

/* Append dir to the matched array unless it is already there.  Takes
 * ownership of dir either way. */
static void matched_add(const char ***matched, int *n, int *alloc, char *dir)
{
    for (int i = 0; i < *n; i++) {
        if (strcmp((*matched)[i], dir) == 0) {
            free(dir);
            return;
        }
    }
    if (*n >= *alloc) {
        *alloc = *alloc ? *alloc * 2 : 8;
        *matched = aept_realloc(*matched, *alloc * sizeof(char *));
    }
    (*matched)[(*n)++] = dir;
}

/* Merge the directories a previous failed run left on record into the
 * matched set, so a retry is owed everything the original run was. */
static void merge_pending_dirs(struct aept_ctx *ctx, const char *pkg, const char ***matched, int *n,
                               int *alloc)
{
    char *path = pending_path(ctx, pkg);
    FILE *fp = fopen(path, "r");
    char buf[4096];

    free(path);
    if (!fp)
        return;

    while (fgets(buf, sizeof(buf), fp)) {
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        buf[strcspn(buf, "\n")] = '\0';
        if (buf[0] != '/')
            continue;
        matched_add(matched, n, alloc, aept_strdup(buf));
    }

    fclose(fp);
}

static int write_pending_file(struct aept_ctx *ctx, const char *pkg, const char **dirs, int n)
{
    char *path = pending_path(ctx, pkg);
    char *tmp = NULL;
    FILE *fp;
    int r = -1;

    aept_asprintf(&tmp, "%s.%d", path, (int)getpid());
    fp = fopen(tmp, "w");
    if (fp) {
        for (int i = 0; i < n; i++)
            fprintf(fp, "%s\n", dirs[i]);
        if (!ferror(fp) && fclose(fp) == 0 && rename(tmp, path) == 0)
            r = 0;
        else
            unlink(tmp);
    }
    if (r != 0)
        aept_log_warning("cannot record pending trigger for '%s'", pkg);

    free(tmp);
    free(path);
    return r;
}

static void clear_pending(struct aept_ctx *ctx, const char *pkg)
{
    char *path = pending_path(ctx, pkg);
    char state[64];

    unlink(path);
    free(path);

    /* Only a state this machinery set is restored: a package sitting
     * at "unpacked" owes a postinst, and a completed trigger must not
     * launder that away. */
    if (aept_status_get_state(ctx, pkg, state, sizeof(state)) == 0 &&
        strcmp(state, "triggers-pending") == 0)
        aept_status_set_state(ctx, pkg, "installed");
}

static int run_trigger_script(struct aept_ctx *ctx, const char *pkg_name, const char **dirs,
                              int n_dirs)
{
    char *path = NULL;
    int r;

    aept_asprintf(&path, "%s/%s.trigger", ctx->config.info_dir, pkg_name);

    if (!aept_file_exists(path)) {
        free(path);
        return 0;
    }

    aept_log_info("running trigger for %s", pkg_name);

    const char *run_path = strip_offline_root(ctx, path);

    /* Build argv: /bin/sh <script> <dir1> <dir2> ... NULL */
    int argc = 2 + n_dirs;
    const char **argv = aept_malloc((argc + 1) * sizeof(char *));
    argv[0] = "/bin/sh";
    argv[1] = run_path;
    for (int i = 0; i < n_dirs; i++)
        argv[2 + i] = dirs[i];
    argv[argc] = NULL;

    r = aept_system_offline_root(ctx, argv);

    free(argv);
    free(path);

    if (r != 0) {
        aept_log_error("trigger script for %s failed with exit code %d", pkg_name, r);
        return r;
    }

    return 0;
}

/*
 * One package's triggers, with the failure persisted: the directories
 * owed are written to {name}.triggers-pending *before* the script
 * runs -- so a failure, a crash or a Ctrl-C all leave the same record
 * behind, and the next transaction (or `aept triggers`) retries with
 * exactly what this run was owed.  On success the record is removed
 * and the Status line restored.  The side file is authoritative; the
 * Status line is display, repaired from the file whenever they
 * disagree.  Returns 0 on success, 1 on a failed script.
 */
static int run_pkg_triggers(struct aept_ctx *ctx, const char *pkg, const char **dirs, int n_dirs)
{
    char state[64];

    write_pending_file(ctx, pkg, dirs, n_dirs);
    if (aept_status_get_state(ctx, pkg, state, sizeof(state)) == 0 &&
        strcmp(state, "installed") == 0)
        aept_status_set_state(ctx, pkg, "triggers-pending");

    if (run_trigger_script(ctx, pkg, dirs, n_dirs) != 0)
        return 1;

    clear_pending(ctx, pkg);
    return 0;
}

/* A single trigger pattern entry. */
typedef struct {
    char *pattern;
    char *pkg_name;
    int modify_only; /* pattern had '+' prefix */
} trigger_entry_t;

/* Scan info_dir for *.triggers files and build the entry list on the
 * fly, without an intermediate index file on disk. */
static void load_trigger_entries(struct aept_ctx *ctx, trigger_entry_t **out_entries,
                                 int *out_count)
{
    DIR *dp;
    struct dirent *de;
    trigger_entry_t *entries = NULL;
    int n_entries = 0;
    int entries_alloc = 0;

    dp = opendir(ctx->config.info_dir);
    if (!dp) {
        *out_entries = NULL;
        *out_count = 0;
        return;
    }

    while ((de = readdir(dp)) != NULL) {
        const char *suffix = ".triggers";
        size_t nlen = strlen(de->d_name);
        size_t slen = strlen(suffix);

        if (nlen <= slen)
            continue;
        if (strcmp(de->d_name + nlen - slen, suffix) != 0)
            continue;

        char *pkg_name = strndup(de->d_name, nlen - slen);
        if (!pkg_name)
            continue;

        char *trig_path = NULL;
        aept_asprintf(&trig_path, "%s/%s", ctx->config.info_dir, de->d_name);

        FILE *tfp = fopen(trig_path, "r");
        free(trig_path);

        if (!tfp) {
            free(pkg_name);
            continue;
        }

        char line[4096];
        while (fgets(line, sizeof(line), tfp)) {
            /* Without this, an over-long line is handed back in pieces
             * and every piece becomes a pattern of its own — a trigger
             * the package never declared. */
            if (aept_fgets_is_truncated(line, sizeof(line))) {
                aept_log_warning("ignoring over-long trigger pattern in "
                                 "'%s.triggers'",
                                 pkg_name);
                aept_fgets_drain_line(tfp);
                continue;
            }

            line[strcspn(line, "\n")] = '\0';

            const char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '\0' || *p == '#')
                continue;

            if (n_entries >= entries_alloc) {
                entries_alloc = entries_alloc ? entries_alloc * 2 : 16;
                entries = aept_realloc(entries, entries_alloc * sizeof(*entries));
            }

            int modify_only = 0;
            if (p[0] == '+') {
                modify_only = 1;
                p++;
            }

            entries[n_entries].pattern = aept_strdup(p);
            entries[n_entries].pkg_name = aept_strdup(pkg_name);
            entries[n_entries].modify_only = modify_only;
            n_entries++;
        }

        fclose(tfp);
        free(pkg_name);
    }

    closedir(dp);

    *out_entries = entries;
    *out_count = n_entries;
}

static int retry_pending_scan(struct aept_ctx *ctx, char **skip, int n_skip);

int aept_trigger_run_all(struct aept_ctx *ctx, aept_trigger_ctx_t *tctx)
{
    trigger_entry_t *entries = NULL;
    int n_entries = 0;
    int failures = 0;
    char **processed = NULL;
    int n_processed = 0, processed_alloc = 0;

    if (tctx->n_dirs == 0)
        goto retry_leftover;

    /* Sort & deduplicate collected directories */
    sort_and_dedup(tctx->dirs, &tctx->n_dirs);

    load_trigger_entries(ctx, &entries, &n_entries);

    if (n_entries == 0)
        goto retry_leftover;

    /* Process entries grouped by package.  Since the set is small,
     * a simple O(n*m) approach is fine: for each unique package,
     * collect matching directories across all its patterns. */
    for (int i = 0; i < n_entries; i++) {
        if (!entries[i].pkg_name)
            continue; /* already processed */

        const char *pkg = entries[i].pkg_name;
        int pkg_is_fresh = is_fresh(tctx, pkg);

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
            for (int d = 0; d < tctx->n_dirs; d++) {
                /* Trigger patterns are absolute, dirs are relative */
                char *abs_dir = NULL;
                aept_asprintf(&abs_dir, "/%s", tctx->dirs[d]);

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
                            matched_alloc = matched_alloc ? matched_alloc * 2 : 8;
                            matched = aept_realloc(matched, matched_alloc * sizeof(char *));
                        }
                        matched[n_matched++] = abs_dir;
                        abs_dir = NULL; /* ownership transferred */
                    }
                }

                free(abs_dir);
            }

            /* For fresh packages with non-modify-only patterns:
             * if the pattern is a concrete path and exists on disk,
             * add it even if it wasn't in tctx->dirs. */
            if (pkg_is_fresh && !entries[e].modify_only && !has_glob_chars(pat)) {
                char *full_path = NULL;
                if (ctx->config.offline_root)
                    aept_asprintf(&full_path, "%s%s", ctx->config.offline_root, pat);
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
                            matched_alloc = matched_alloc ? matched_alloc * 2 : 8;
                            matched = aept_realloc(matched, matched_alloc * sizeof(char *));
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

        /* A failed earlier run left its directories on record; the
         * retry is owed those on top of anything matched now. */
        merge_pending_dirs(ctx, pkg, &matched, &n_matched, &matched_alloc);

        if (n_matched > 0)
            failures += run_pkg_triggers(ctx, pkg, matched, n_matched);

        if (n_processed >= processed_alloc) {
            processed_alloc = processed_alloc ? processed_alloc * 2 : 8;
            processed = aept_realloc(processed, processed_alloc * sizeof(char *));
        }
        processed[n_processed++] = aept_strdup(pkg);

        for (int m = 0; m < n_matched; m++)
            free((char *)matched[m]);
        free(matched);

        free(entries[i].pkg_name);
        entries[i].pkg_name = NULL;
    }

    for (int i = 0; i < n_entries; i++) {
        free(entries[i].pattern);
        free(entries[i].pkg_name);
    }
    free(entries);

retry_leftover:
    /* Records for packages this transaction gave nothing new -- their
     * watch did not match, or their .triggers is gone -- still owe a
     * retry.  Everything processed above is skipped: it already ran,
     * and running a failed script twice per transaction reports the
     * same failure twice. */
    failures += retry_pending_scan(ctx, processed, n_processed);

    for (int i = 0; i < n_processed; i++)
        free(processed[i]);
    free(processed);

    return failures;
}

/*
 * Retry every {name}.triggers-pending record except the skipped names.
 * Returns the number of scripts that failed (again).
 */
static int retry_pending_scan(struct aept_ctx *ctx, char **skip, int n_skip)
{
    DIR *dp;
    struct dirent *de;
    char **pkgs = NULL;
    int n_pkgs = 0, pkgs_alloc = 0;
    int failures = 0;

    dp = opendir(ctx->config.info_dir);
    if (!dp)
        return 0;

    /*
     * Collect first, run afterwards: the scripts rewrite files in this
     * very directory (the pending record, the .control), and mutating
     * a directory mid-readdir() can hand entries back again.
     */
    while ((de = readdir(dp)) != NULL) {
        const char *suffix = ".triggers-pending";
        size_t nlen = strlen(de->d_name);
        size_t slen = strlen(suffix);
        int skipped = 0;

        if (nlen <= slen || strcmp(de->d_name + nlen - slen, suffix) != 0)
            continue;

        char *pkg = strndup(de->d_name, nlen - slen);
        if (!pkg)
            continue;

        for (int i = 0; i < n_skip; i++) {
            if (strcmp(skip[i], pkg) == 0) {
                skipped = 1;
                break;
            }
        }
        if (skipped || !aept_pkg_name_is_safe(pkg)) {
            free(pkg);
            continue;
        }

        if (n_pkgs >= pkgs_alloc) {
            pkgs_alloc = pkgs_alloc ? pkgs_alloc * 2 : 8;
            pkgs = aept_realloc(pkgs, pkgs_alloc * sizeof(char *));
        }
        pkgs[n_pkgs++] = pkg;
    }

    closedir(dp);

    for (int p = 0; p < n_pkgs; p++) {
        const char **dirs = NULL;
        int n_dirs = 0, dirs_alloc = 0;

        merge_pending_dirs(ctx, pkgs[p], &dirs, &n_dirs, &dirs_alloc);

        if (n_dirs > 0)
            failures += run_pkg_triggers(ctx, pkgs[p], dirs, n_dirs);
        else
            clear_pending(ctx, pkgs[p]); /* an empty record is owed nothing */

        for (int i = 0; i < n_dirs; i++)
            free((char *)dirs[i]);
        free(dirs);
        free(pkgs[p]);
    }
    free(pkgs);

    return failures;
}

int aept_trigger_retry_pending(struct aept_ctx *ctx)
{
    return retry_pending_scan(ctx, NULL, 0);
}
