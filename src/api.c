/* api.c - public API implementation
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <solv/evr.h>
#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/solvable.h>

#include "aept/aept.h"
#include "aept/internal.h"
#include "aept/autoremove.h"
#include "aept/clean.h"
#include "aept/config.h"
#include "aept/install.h"
#include "aept/msg.h"
#include "aept/pin.h"
#include "aept/query.h"
#include "aept/remove.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/update.h"
#include "aept/util.h"

/* ── Context definition ──────────────────────────────────────────── */

struct aept_ctx {
    aept_config_t config;
    int config_loaded;

    aept_log_fn     log_fn;
    void           *log_userdata;

    aept_confirm_fn confirm_fn;
    void           *confirm_userdata;

    /* Saved globals for restore on deactivate */
    aept_config_t  *saved_cfg;
    aept_log_fn     saved_log_cb;
    void           *saved_log_cb_data;
    aept_confirm_fn saved_confirm_cb;
    void           *saved_confirm_cb_data;
};

/* ── Activate / deactivate ───────────────────────────────────────── */

static void ctx_activate(aept_ctx_t *ctx)
{
    ctx->saved_cfg = aept_cfg;
    ctx->saved_log_cb = aept_log_cb;
    ctx->saved_log_cb_data = aept_log_cb_data;
    ctx->saved_confirm_cb = aept_confirm_cb;
    ctx->saved_confirm_cb_data = aept_confirm_cb_data;

    aept_cfg = &ctx->config;
    aept_log_cb = ctx->log_fn;
    aept_log_cb_data = ctx->log_userdata;
    aept_confirm_cb = ctx->confirm_fn;
    aept_confirm_cb_data = ctx->confirm_userdata;
}

static void ctx_deactivate(aept_ctx_t *ctx)
{
    aept_cfg = ctx->saved_cfg;
    aept_log_cb = ctx->saved_log_cb;
    aept_log_cb_data = ctx->saved_log_cb_data;
    aept_confirm_cb = ctx->saved_confirm_cb;
    aept_confirm_cb_data = ctx->saved_confirm_cb_data;
    aept_confirm_txn = NULL;
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

aept_ctx_t *aept_ctx_new(void)
{
    aept_ctx_t *ctx = calloc(1, sizeof(*ctx));
    return ctx;
}

void aept_ctx_free(aept_ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->config_loaded) {
        aept_config_t *saved = aept_cfg;
        aept_cfg = &ctx->config;
        aept_config_free();
        aept_cfg = saved;
    }

    free(ctx);
}

/* ── Configuration ───────────────────────────────────────────────── */

int aept_ctx_load_config(aept_ctx_t *ctx, const char *path)
{
    aept_config_t *saved = aept_cfg;
    int r = 0;

    if (ctx->config_loaded) {
        aept_cfg = &ctx->config;
        aept_config_free();
        ctx->config_loaded = 0;
    }

    aept_cfg = &ctx->config;

    if (!path)
        path = "/etc/aept/aept.conf";

    if (access(path, R_OK) < 0 && errno == ENOENT)
        aept_config_set_defaults();
    else
        r = aept_config_load(path);

    if (r < 0) {
        aept_cfg = saved;
        return -1;
    }

    aept_config_apply_offline_root();
    ctx->config_loaded = 1;

    aept_cfg = saved;
    return 0;
}

void aept_ctx_set_offline_root(aept_ctx_t *ctx, const char *path)
{
    free(ctx->config.offline_root);
    ctx->config.offline_root = path ? aept_strdup(path) : NULL;
}

void aept_ctx_set_verbosity(aept_ctx_t *ctx, int level)
{
    ctx->config.verbosity = level;
}

/* ── Flags ───────────────────────────────────────────────────────── */

static int *flag_ptr(aept_config_t *cfg, int flag)
{
    switch (flag) {
    case AEPT_FLAG_FORCE_DEPENDS:    return &cfg->force_depends;
    case AEPT_FLAG_DOWNLOAD_ONLY:    return &cfg->download_only;
    case AEPT_FLAG_NOACTION:         return &cfg->noaction;
    case AEPT_FLAG_ALLOW_DOWNGRADE:  return &cfg->allow_downgrade;
    case AEPT_FLAG_REINSTALL:        return &cfg->reinstall;
    case AEPT_FLAG_NO_CACHE:         return &cfg->no_cache;
    case AEPT_FLAG_FORCE_CONFNEW:    return &cfg->force_confnew;
    case AEPT_FLAG_FORCE_CONFOLD:    return &cfg->force_confold;
    case AEPT_FLAG_PURGE:            return &cfg->purge;
    case AEPT_FLAG_NON_INTERACTIVE:  return &cfg->non_interactive;
    case AEPT_FLAG_CHECK_SIGNATURE:  return &cfg->check_signature;
    case AEPT_FLAG_IGNORE_UID:       return &cfg->ignore_uid;
    default:                         return NULL;
    }
}

void aept_ctx_set_flag(aept_ctx_t *ctx, int flag, int value)
{
    int *p = flag_ptr(&ctx->config, flag);
    if (p)
        *p = value;
}

int aept_ctx_get_flag(aept_ctx_t *ctx, int flag)
{
    int *p = flag_ptr(&ctx->config, flag);
    return p ? *p : 0;
}

/* ── Callbacks ───────────────────────────────────────────────────── */

void aept_ctx_set_log_fn(aept_ctx_t *ctx, aept_log_fn fn, void *userdata)
{
    ctx->log_fn = fn;
    ctx->log_userdata = userdata;
}

void aept_ctx_set_confirm_fn(aept_ctx_t *ctx, aept_confirm_fn fn,
                             void *userdata)
{
    ctx->confirm_fn = fn;
    ctx->confirm_userdata = userdata;
}

/* ── Mutating operations ─────────────────────────────────────────── */

int aept_update(aept_ctx_t *ctx)
{
    int r;

    ctx_activate(ctx);

    if (aept_config_validate() < 0) { r = -1; goto out; }
    if (aept_config_lock() < 0)     { r = -1; goto out; }

    r = aept_op_update();

    aept_config_unlock();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_install(aept_ctx_t *ctx, const char **names, int name_count,
                 const char **local_paths, int local_count)
{
    int r;

    ctx_activate(ctx);

    if (aept_config_validate() < 0) { r = -1; goto out; }
    if (aept_config_lock() < 0)     { r = -1; goto out; }

    r = aept_op_install(names, name_count, local_paths, local_count);

    aept_config_unlock();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_upgrade(aept_ctx_t *ctx)
{
    int r;

    ctx_activate(ctx);

    if (aept_config_validate() < 0) { r = -1; goto out; }
    if (aept_config_lock() < 0)     { r = -1; goto out; }

    r = aept_op_install(NULL, 0, NULL, 0);

    aept_config_unlock();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_remove(aept_ctx_t *ctx, const char **names, int count)
{
    int r;

    ctx_activate(ctx);

    if (aept_config_validate() < 0) { r = -1; goto out; }
    if (aept_config_lock() < 0)     { r = -1; goto out; }

    r = aept_op_remove(names, count);

    aept_config_unlock();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_autoremove(aept_ctx_t *ctx)
{
    int r;

    ctx_activate(ctx);

    if (aept_config_validate() < 0) { r = -1; goto out; }
    if (aept_config_lock() < 0)     { r = -1; goto out; }

    r = aept_op_autoremove();

    aept_config_unlock();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_clean(aept_ctx_t *ctx)
{
    int r;

    ctx_activate(ctx);

    if (aept_config_validate() < 0) { r = -1; goto out; }
    if (aept_config_lock() < 0)     { r = -1; goto out; }

    r = aept_op_clean();

    aept_config_unlock();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_pin(aept_ctx_t *ctx, const char **specs, int count)
{
    int i, r = 0;
    int solver_ready = 0;

    ctx_activate(ctx);

    for (i = 0; i < count; i++) {
        char *copy = aept_strdup(specs[i]);
        char *eq = strchr(copy, '=');
        const char *name;
        const char *version;

        if (eq) {
            *eq = '\0';
            name = copy;
            version = eq + 1;
        } else {
            name = copy;

            if (!solver_ready) {
                if (aept_solver_init() < 0 || aept_status_load() < 0) {
                    aept_solver_fini();
                    free(copy);
                    r = -1;
                    goto out;
                }
                solver_ready = 1;
            }

            version = aept_solver_installed_version(name);
            if (!version) {
                free(copy);
                continue;
            }
        }

        if (aept_pin_add(name, version) < 0)
            r = -1;

        free(copy);
    }

    if (solver_ready)
        aept_solver_fini();
out:
    ctx_deactivate(ctx);
    return r;
}

int aept_unpin(aept_ctx_t *ctx, const char **names, int count)
{
    int i, r = 0;

    ctx_activate(ctx);

    for (i = 0; i < count; i++) {
        if (aept_pin_remove(names[i]) < 0)
            r = -1;
    }

    ctx_deactivate(ctx);
    return r;
}

int aept_mark_auto(aept_ctx_t *ctx, const char **names, int count)
{
    int i, r = 0;

    ctx_activate(ctx);

    for (i = 0; i < count; i++) {
        char *list_path = NULL;
        aept_asprintf(&list_path, "%s/%s.list",
                  aept_cfg->info_dir, names[i]);
        if (!aept_file_exists(list_path)) {
            free(list_path);
            continue;
        }
        free(list_path);
        if (aept_status_mark_auto(names[i]) < 0)
            r = -1;
    }

    ctx_deactivate(ctx);
    return r;
}

int aept_mark_manual(aept_ctx_t *ctx, const char **names, int count)
{
    int i, r = 0;

    ctx_activate(ctx);

    for (i = 0; i < count; i++) {
        char *list_path = NULL;
        aept_asprintf(&list_path, "%s/%s.list",
                  aept_cfg->info_dir, names[i]);
        if (!aept_file_exists(list_path)) {
            free(list_path);
            continue;
        }
        free(list_path);
        if (aept_status_unmark_auto(names[i]) < 0)
            r = -1;
    }

    ctx_deactivate(ctx);
    return r;
}

int aept_mark_manual_all(aept_ctx_t *ctx)
{
    int r;

    ctx_activate(ctx);
    r = aept_status_clear_auto();
    ctx_deactivate(ctx);
    return r;
}

/* ── Query helpers ───────────────────────────────────────────────── */

static int query_load_repos(void)
{
    int i;

    for (i = 0; i < aept_cfg->nsources; i++) {
        char *list_path = NULL;
        FILE *fp;

        aept_asprintf(&list_path, "%s/%s",
                  aept_cfg->lists_dir, aept_cfg->sources[i].name);

        fp = fopen(list_path, "r");
        if (!fp) {
            free(list_path);
            continue;
        }

        aept_solver_load_repo(aept_cfg->sources[i].name, fp, i);
        fclose(fp);
        free(list_path);
    }

    return 0;
}

static char *deparray_to_str(Pool *pool, Solvable *s, Id keyname, Id marker)
{
    Queue q;
    int i;
    size_t len;
    char *result, *p;

    queue_init(&q);
    solvable_lookup_deparray(s, keyname, &q, marker);

    if (q.count == 0) {
        queue_free(&q);
        return NULL;
    }

    len = 0;
    for (i = 0; i < q.count; i++) {
        if (i > 0) len += 2;
        len += strlen(pool_dep2str(pool, q.elements[i]));
    }

    result = malloc(len + 1);
    if (!result) {
        queue_free(&q);
        return NULL;
    }

    p = result;
    for (i = 0; i < q.count; i++) {
        const char *dep = pool_dep2str(pool, q.elements[i]);
        size_t dlen = strlen(dep);
        if (i > 0) {
            *p++ = ',';
            *p++ = ' ';
        }
        memcpy(p, dep, dlen);
        p += dlen;
    }
    *p = '\0';

    queue_free(&q);
    return result;
}

/* ── Query: list ─────────────────────────────────────────────────── */

struct api_list_entry {
    Id name_id;
    Solvable *avail;
    Solvable *installed;
};

static struct api_list_entry *find_list_entry(struct api_list_entry *entries,
                                              int n, Id name_id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (entries[i].name_id == name_id)
            return &entries[i];
    }
    return NULL;
}

static Pool *api_sort_pool;

static int cmp_api_list_entry(const void *a, const void *b)
{
    const struct api_list_entry *ea = a;
    const struct api_list_entry *eb = b;
    return strcmp(pool_id2str(api_sort_pool, ea->name_id),
                 pool_id2str(api_sort_pool, eb->name_id));
}

int aept_list(aept_ctx_t *ctx, const char *pattern,
              int filter_installed, int filter_upgradable,
              aept_pkg_list_t *out)
{
    Pool *pool;
    Id p;
    Solvable *s;
    struct api_list_entry *entries = NULL;
    int nentries = 0, alloc = 0;
    int i, r = -1;

    memset(out, 0, sizeof(*out));
    ctx_activate(ctx);

    if (aept_solver_init() < 0)
        goto out;

    aept_status_load();
    query_load_repos();

    pool = aept_solver_pool();

    FOR_POOL_SOLVABLES(p) {
        struct api_list_entry *e;

        s = pool_id2solvable(pool, p);
        e = find_list_entry(entries, nentries, s->name);

        if (!e) {
            if (nentries >= alloc) {
                alloc = alloc ? alloc * 2 : 256;
                entries = aept_realloc(entries, alloc * sizeof(*entries));
            }
            e = &entries[nentries++];
            e->name_id = s->name;
            e->avail = NULL;
            e->installed = NULL;
        }

        if (s->repo == pool->installed) {
            e->installed = s;
        } else {
            if (!e->avail || pool_evrcmp_str(pool,
                    pool_id2str(pool, s->evr),
                    pool_id2str(pool, e->avail->evr), EVRCMP_COMPARE) > 0)
                e->avail = s;
        }
    }

    api_sort_pool = pool;
    qsort(entries, nentries, sizeof(*entries), cmp_api_list_entry);

    out->entries = calloc(nentries, sizeof(aept_pkg_entry_t));
    if (!out->entries && nentries > 0)
        goto cleanup;

    for (i = 0; i < nentries; i++) {
        struct api_list_entry *e = &entries[i];
        const char *name = pool_id2str(pool, e->name_id);
        const char *summary;
        Solvable *show;
        int upgradable;

        if (pattern && fnmatch(pattern, name, 0) != 0)
            continue;

        if (filter_installed && !e->installed)
            continue;

        upgradable = e->installed && e->avail &&
            pool_evrcmp_str(pool,
                pool_id2str(pool, e->avail->evr),
                pool_id2str(pool, e->installed->evr), EVRCMP_COMPARE) > 0;

        if (filter_upgradable && !upgradable)
            continue;

        show = filter_installed ? e->installed :
               (e->avail ? e->avail : e->installed);

        aept_pkg_entry_t *pe = &out->entries[out->count++];
        pe->name = strdup(pool_id2str(pool, e->name_id));
        pe->version = strdup(pool_id2str(pool, show->evr));
        summary = solvable_lookup_str(show, SOLVABLE_SUMMARY);
        pe->summary = summary ? strdup(summary) : NULL;
        pe->installed = e->installed != NULL;
        pe->upgradable = upgradable;
    }

    r = 0;

cleanup:
    free(entries);
    aept_solver_fini();
out:
    ctx_deactivate(ctx);
    return r;
}

void aept_pkg_list_free(aept_pkg_list_t *list)
{
    int i;

    if (!list)
        return;

    for (i = 0; i < list->count; i++) {
        free(list->entries[i].name);
        free(list->entries[i].version);
        free(list->entries[i].summary);
    }
    free(list->entries);
    memset(list, 0, sizeof(*list));
}

/* ── Query: show ─────────────────────────────────────────────────── */

int aept_show(aept_ctx_t *ctx, const char *name, aept_pkg_info_t *out)
{
    Pool *pool;
    Id name_id, p;
    Solvable *s, *best = NULL, *installed = NULL;
    const char *str;
    unsigned int medianr;
    int r = -1;

    memset(out, 0, sizeof(*out));
    ctx_activate(ctx);

    if (aept_solver_init() < 0)
        goto out;

    aept_status_load();
    query_load_repos();

    pool = aept_solver_pool();

    name_id = pool_str2id(pool, name, 0);
    if (!name_id)
        goto not_found;

    FOR_POOL_SOLVABLES(p) {
        s = pool_id2solvable(pool, p);
        if (s->name != name_id)
            continue;

        if (s->repo == pool->installed) {
            installed = s;
        } else {
            if (!best || pool_evrcmp_str(pool,
                    pool_id2str(pool, s->evr),
                    pool_id2str(pool, best->evr), EVRCMP_COMPARE) > 0)
                best = s;
        }
    }

    s = best ? best : installed;
    if (!s)
        goto not_found;

    out->name = strdup(pool_id2str(pool, s->name));
    out->version = strdup(pool_id2str(pool, s->evr));
    out->architecture = strdup(pool_id2str(pool, s->arch));
    out->installed_size = solvable_lookup_num(s, SOLVABLE_INSTALLSIZE, 0);

    out->depends = deparray_to_str(pool, s, SOLVABLE_REQUIRES,
                                   -SOLVABLE_PREREQMARKER);
    out->pre_depends = deparray_to_str(pool, s, SOLVABLE_REQUIRES,
                                       SOLVABLE_PREREQMARKER);
    out->recommends = deparray_to_str(pool, s, SOLVABLE_RECOMMENDS, 0);
    out->suggests = deparray_to_str(pool, s, SOLVABLE_SUGGESTS, 0);
    out->provides = deparray_to_str(pool, s, SOLVABLE_PROVIDES,
                                    -SOLVABLE_FILEMARKER);
    out->conflicts = deparray_to_str(pool, s, SOLVABLE_CONFLICTS, 0);
    out->replaces = deparray_to_str(pool, s, SOLVABLE_OBSOLETES, 0);

    str = solvable_lookup_str(s, SOLVABLE_URL);
    out->homepage = str ? strdup(str) : NULL;

    str = solvable_lookup_location(s, &medianr);
    out->filename = str ? strdup(str) : NULL;

    str = solvable_lookup_str(s, SOLVABLE_SUMMARY);
    out->summary = str ? strdup(str) : NULL;

    str = solvable_lookup_str(s, SOLVABLE_DESCRIPTION);
    out->description = str ? strdup(str) : NULL;

    out->is_installed = installed != NULL;

    r = 0;
    goto cleanup;

not_found:
    r = 1;
cleanup:
    aept_solver_fini();
out:
    ctx_deactivate(ctx);
    return r;
}

void aept_pkg_info_free(aept_pkg_info_t *info)
{
    if (!info)
        return;

    free(info->name);
    free(info->version);
    free(info->architecture);
    free(info->depends);
    free(info->pre_depends);
    free(info->recommends);
    free(info->suggests);
    free(info->provides);
    free(info->conflicts);
    free(info->replaces);
    free(info->homepage);
    free(info->filename);
    free(info->summary);
    free(info->description);
    memset(info, 0, sizeof(*info));
}

/* ── Query: files ────────────────────────────────────────────────── */

int aept_files(aept_ctx_t *ctx, const char *name,
               char ***paths_out, int *count_out)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];
    char **paths = NULL;
    int count = 0, alloc = 0;
    int r = -1;

    *paths_out = NULL;
    *count_out = 0;

    ctx_activate(ctx);

    if (!aept_pkg_name_is_safe(name)) {
        r = -1;
        goto out;
    }

    aept_asprintf(&list_path, "%s/%s.list", aept_cfg->info_dir, name);

    fp = fopen(list_path, "r");
    free(list_path);

    if (!fp) {
        r = 1;
        goto out;
    }

    while (fgets(buf, sizeof(buf), fp)) {
        char *tab;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }
        buf[strcspn(buf, "\n")] = '\0';

        tab = strchr(buf, '\t');
        if (tab)
            *tab = '\0';

        if (*buf == '\0')
            continue;

        if (count >= alloc) {
            alloc = alloc ? alloc * 2 : 64;
            paths = realloc(paths, alloc * sizeof(char *));
            if (!paths) {
                fclose(fp);
                goto out;
            }
        }
        paths[count++] = strdup(buf);
    }

    fclose(fp);

    *paths_out = paths;
    *count_out = count;
    r = 0;

out:
    ctx_deactivate(ctx);
    return r;
}

/* ── Query: owns ─────────────────────────────────────────────────── */

static const char *strip_leading(const char *p)
{
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    while (p[0] == '/')
        p++;
    return p;
}

static size_t owns_path_len(const char *p)
{
    size_t len = strlen(p);
    while (len > 1 && p[len - 1] == '/')
        len--;
    return len;
}

int aept_owns(aept_ctx_t *ctx, const char *path,
              char ***owners_out, int *count_out)
{
    DIR *dir;
    struct dirent *ent;
    const char *needle;
    size_t needle_len;
    char **owners = NULL;
    int count = 0, alloc = 0;
    int r;

    *owners_out = NULL;
    *count_out = 0;

    ctx_activate(ctx);

    if (!path || *path == '\0') {
        r = -1;
        goto out;
    }

    needle = strip_leading(path);
    if (*needle == '\0')
        needle = ".";
    needle_len = owns_path_len(needle);

    dir = opendir(aept_cfg->info_dir);
    if (!dir) {
        r = 1;
        goto out;
    }

    while ((ent = readdir(dir)) != NULL) {
        const char *dot;
        char *list_path = NULL;
        FILE *fp;
        char buf[4096];

        dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".list") != 0)
            continue;

        aept_asprintf(&list_path, "%s/%s", aept_cfg->info_dir, ent->d_name);
        fp = fopen(list_path, "r");
        free(list_path);

        if (!fp)
            continue;

        while (fgets(buf, sizeof(buf), fp)) {
            const char *entry;
            char *tab;

            if (aept_fgets_is_truncated(buf, sizeof(buf))) {
                aept_fgets_drain_line(fp);
                continue;
            }
            buf[strcspn(buf, "\n")] = '\0';

            tab = strchr(buf, '\t');
            if (tab)
                *tab = '\0';

            if (*buf == '\0')
                continue;

            entry = strip_leading(buf);
            if (*entry == '\0')
                entry = ".";

            if (owns_path_len(entry) == needle_len &&
                strncmp(entry, needle, needle_len) == 0) {
                size_t name_len = (size_t)(dot - ent->d_name);

                if (count >= alloc) {
                    alloc = alloc ? alloc * 2 : 4;
                    owners = realloc(owners, alloc * sizeof(char *));
                }
                char *owner = malloc(name_len + 1);
                if (owner) {
                    memcpy(owner, ent->d_name, name_len);
                    owner[name_len] = '\0';
                    owners[count++] = owner;
                }
                break;
            }
        }

        fclose(fp);
    }

    closedir(dir);

    *owners_out = owners;
    *count_out = count;
    r = count > 0 ? 0 : 1;

out:
    ctx_deactivate(ctx);
    return r;
}

/* ── Query: architectures ────────────────────────────────────────── */

int aept_architectures(aept_ctx_t *ctx, char ***archs_out, int *count_out)
{
    int i;

    *archs_out = NULL;
    *count_out = 0;

    ctx_activate(ctx);

    if (aept_cfg->narchs > 0) {
        char **archs = malloc(aept_cfg->narchs * sizeof(char *));
        if (!archs) {
            ctx_deactivate(ctx);
            return -1;
        }

        for (i = 0; i < aept_cfg->narchs; i++)
            archs[i] = strdup(aept_cfg->archs[i]);

        *archs_out = archs;
        *count_out = aept_cfg->narchs;
    }

    ctx_deactivate(ctx);
    return 0;
}
