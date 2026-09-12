/* api.c - public API implementation
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
#include <unistd.h>

#include <solv/evr.h>
#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/queue.h>
#include <solv/solvable.h>

#include "libfetch/fetch.h"

#include "aept/aept.h"
#include "aept/internal.h"
#include "aept/autoremove.h"
#include "aept/clean.h"
#include "aept/config.h"
#include "aept/index.h"
#include "aept/install.h"
#include "aept/msg.h"
#include "aept/pin.h"
#include "aept/remove.h"
#include "aept/solver.h"
#include "aept/stanza.h"
#include "aept/status.h"
#include "aept/trigger.h"
#include "aept/update.h"
#include "aept/util.h"

/* ── Lifecycle ───────────────────────────────────────────────────── */

aept_ctx_t *aept_init(void)
{
    /* Plain malloc: this is the call that creates the context, so there
     * is no armed entry point to unwind to yet.  The NULL return is
     * already part of the API. */
    aept_ctx_t *ctx = malloc(sizeof(*ctx));

    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->lock_fd = -1;
    ctx->use_color = isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
    aept_log_set_ctx(ctx);
    ctx->http = libfetch_ctx_new(4, 2);
    if (!ctx->http) {
        aept_log_set_ctx(NULL);
        free(ctx);
        return NULL;
    }
    /* A caller that never loads a config file still gets the default
     * timeout rather than an unbounded wait.  Set directly rather than
     * through aept_config_set_defaults(), which allocates the path
     * strings that only aept_load_config() knows how to free. */
    ctx->config.network_timeout = AEPT_DEFAULT_NETWORK_TIMEOUT;
    libfetch_set_timeout(ctx->http, ctx->config.network_timeout);
    return ctx;
}

void aept_cleanup(aept_ctx_t *ctx)
{
    if (!ctx)
        return;

    libfetch_ctx_free(ctx->http);
    ctx->http = NULL;

    if (ctx->config_loaded) {
        aept_config_free(&ctx->config);
        ctx->config_loaded = 0;
    }

    aept_log_set_ctx(NULL);
    free(ctx);
}

/* ── Configuration ───────────────────────────────────────────────── */

static int api_load_config(aept_ctx_t *ctx, const char *path)
{
    char *root_override = ctx->config.offline_root;
    int r = 0;

    ctx->config.offline_root = NULL;

    if (ctx->config_loaded) {
        aept_config_free(&ctx->config);
        ctx->config_loaded = 0;
    }

    if (!path)
        path = "/etc/aept/aept.conf";

    if (access(path, R_OK) < 0 && errno == ENOENT)
        aept_config_set_defaults(&ctx->config);
    else
        r = aept_config_load(&ctx->config, path);

    if (r < 0) {
        free(root_override);
        return -1;
    }

    if (root_override) {
        free(ctx->config.offline_root);
        ctx->config.offline_root = root_override;
    }

    aept_config_apply_offline_root(&ctx->config);
    ctx->config_loaded = 1;

    libfetch_set_timeout(ctx->http, ctx->config.network_timeout);

    return 0;
}

int aept_load_config(aept_ctx_t *ctx, const char *path)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_load_config(ctx, path);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

int aept_set_offline_root(aept_ctx_t *ctx, const char *path)
{
    char *copy;
    AEPT_OOM_ENTER(ctx, -1);

    /* Copy before releasing: a failure unwinds out of here, and freeing
     * first would leave the field dangling rather than unchanged. */
    copy = path ? aept_strdup(path) : NULL;
    free(ctx->config.offline_root);
    ctx->config.offline_root = copy;
    AEPT_OOM_LEAVE(ctx);
    return 0;
}

int aept_set_cache_dir(aept_ctx_t *ctx, const char *path)
{
    char *copy;
    AEPT_OOM_ENTER(ctx, -1);

    /* Copy before releasing: a failure unwinds out of here, and freeing
     * first would leave the field dangling rather than unchanged. */
    copy = path ? aept_strdup(path) : NULL;
    free(ctx->config.cache_dir);
    ctx->config.cache_dir = copy;
    AEPT_OOM_LEAVE(ctx);
    return 0;
}

void aept_set_verbosity(aept_ctx_t *ctx, int level)
{
    ctx->config.verbosity = level;
}

void aept_set_network_timeout(aept_ctx_t *ctx, int seconds)
{
    if (seconds < 0)
        seconds = 0;
    ctx->config.network_timeout = seconds;
    libfetch_set_timeout(ctx->http, seconds);
}

int aept_last_error(aept_ctx_t *ctx)
{
    return ctx->last_error;
}

/* ── Flags ───────────────────────────────────────────────────────── */

static int *flag_ptr(aept_config_t *cfg, int flag)
{
    switch (flag) {
    case AEPT_FLAG_FORCE_DEPENDS:
        return &cfg->force_depends;
    case AEPT_FLAG_DOWNLOAD_ONLY:
        return &cfg->download_only;
    case AEPT_FLAG_NOACTION:
        return &cfg->noaction;
    case AEPT_FLAG_ALLOW_DOWNGRADE:
        return &cfg->allow_downgrade;
    case AEPT_FLAG_REINSTALL:
        return &cfg->reinstall;
    case AEPT_FLAG_NO_CACHE:
        return &cfg->no_cache;
    case AEPT_FLAG_FORCE_CONFNEW:
        return &cfg->force_confnew;
    case AEPT_FLAG_FORCE_CONFOLD:
        return &cfg->force_confold;
    case AEPT_FLAG_PURGE:
        return &cfg->purge;
    case AEPT_FLAG_NON_INTERACTIVE:
        return &cfg->non_interactive;
    case AEPT_FLAG_CHECK_SIGNATURE:
        return &cfg->check_signature;
    case AEPT_FLAG_IGNORE_UID:
        return &cfg->ignore_uid;
    case AEPT_FLAG_KEEP_GOING:
        return &cfg->keep_going;
    default:
        return NULL;
    }
}

void aept_set_flag(aept_ctx_t *ctx, int flag, int value)
{
    int *p = flag_ptr(&ctx->config, flag);
    if (p)
        *p = value;
}

int aept_get_flag(aept_ctx_t *ctx, int flag)
{
    int *p = flag_ptr(&ctx->config, flag);
    return p ? *p : 0;
}

/* ── Callbacks ───────────────────────────────────────────────────── */

void aept_set_log_fn(aept_ctx_t *ctx, aept_log_fn fn, void *userdata)
{
    ctx->log_fn = fn;
    ctx->log_userdata = userdata;
}

void aept_set_display_fn(aept_ctx_t *ctx, aept_display_fn fn, void *userdata)
{
    ctx->display_fn = fn;
    ctx->display_userdata = userdata;
}

void aept_set_confirm_fn(aept_ctx_t *ctx, aept_confirm_fn fn, void *userdata)
{
    ctx->confirm_fn = fn;
    ctx->confirm_userdata = userdata;
}

/* ── Cancellation ────────────────────────────────────────────────── */

void aept_cancel(aept_ctx_t *ctx)
{
    ctx->cancelled = 1;
}

/* ── Mutating operations ─────────────────────────────────────────── */

static int api_update(aept_ctx_t *ctx)
{
    int r;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    r = aept_op_update(ctx);

    aept_config_unlock(ctx);
    return r;
}

int aept_update(aept_ctx_t *ctx)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_update(ctx);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_install(aept_ctx_t *ctx, const char **names, int name_count,
                       const char **local_paths, int local_count)
{
    int r;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    r = aept_op_install(ctx, names, name_count, local_paths, local_count);

    aept_config_unlock(ctx);
    return r;
}

int aept_install(aept_ctx_t *ctx, const char **names, int name_count, const char **local_paths,
                 int local_count)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_install(ctx, names, name_count, local_paths, local_count);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_upgrade(aept_ctx_t *ctx)
{
    int r;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    r = aept_op_install(ctx, NULL, 0, NULL, 0);

    aept_config_unlock(ctx);
    return r;
}

int aept_upgrade(aept_ctx_t *ctx)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_upgrade(ctx);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_remove(aept_ctx_t *ctx, const char **names, int count)
{
    int r;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    r = aept_op_remove(ctx, names, count);

    aept_config_unlock(ctx);
    return r;
}

int aept_remove(aept_ctx_t *ctx, const char **names, int count)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_remove(ctx, names, count);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_autoremove(aept_ctx_t *ctx)
{
    int r;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    r = aept_op_autoremove(ctx);

    aept_config_unlock(ctx);
    return r;
}

int aept_autoremove(aept_ctx_t *ctx)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_autoremove(ctx);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_triggers(aept_ctx_t *ctx)
{
    int failures;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    failures = aept_trigger_retry_pending(ctx);

    aept_config_unlock(ctx);
    return failures > 0 ? -1 : 0;
}

int aept_triggers(aept_ctx_t *ctx)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_triggers(ctx);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_clean(aept_ctx_t *ctx)
{
    int r;

    if (aept_config_validate(&ctx->config) < 0)
        return -1;
    if (aept_config_lock(ctx) < 0)
        return -1;

    r = aept_op_clean(ctx);

    aept_config_unlock(ctx);
    return r;
}

int aept_clean(aept_ctx_t *ctx)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_clean(ctx);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_pin(aept_ctx_t *ctx, const char **specs, int count)
{
    int i, r = 0;
    int solver_ready = 0;

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
                if (aept_solver_init(ctx) < 0 || aept_status_load(ctx) < 0) {
                    aept_solver_fini(ctx);
                    free(copy);
                    return -1;
                }
                solver_ready = 1;
            }

            version = aept_solver_installed_version(ctx->solver, name);
            if (!version) {
                free(copy);
                continue;
            }
        }

        if (aept_pin_add(ctx, name, version) < 0)
            r = -1;

        free(copy);
    }

    if (solver_ready)
        aept_solver_fini(ctx);

    return r;
}

int aept_pin(aept_ctx_t *ctx, const char **specs, int count)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_pin(ctx, specs, count);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_unpin(aept_ctx_t *ctx, const char **names, int count)
{
    int i, r = 0;

    for (i = 0; i < count; i++) {
        if (aept_pin_remove(ctx, names[i]) < 0)
            r = -1;
    }

    return r;
}

int aept_unpin(aept_ctx_t *ctx, const char **names, int count)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_unpin(ctx, names, count);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_mark_auto(aept_ctx_t *ctx, const char **names, int count)
{
    int i, r = 0;

    for (i = 0; i < count; i++) {
        char *list_path = NULL;
        aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, names[i]);
        if (!aept_file_exists(list_path)) {
            free(list_path);
            continue;
        }
        free(list_path);
        if (aept_status_mark_auto(ctx, names[i]) < 0)
            r = -1;
    }

    return r;
}

int aept_mark_auto(aept_ctx_t *ctx, const char **names, int count)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_mark_auto(ctx, names, count);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_mark_manual(aept_ctx_t *ctx, const char **names, int count)
{
    int i, r = 0;

    for (i = 0; i < count; i++) {
        char *list_path = NULL;
        aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, names[i]);
        if (!aept_file_exists(list_path)) {
            free(list_path);
            continue;
        }
        free(list_path);
        if (aept_status_unmark_auto(ctx, names[i]) < 0)
            r = -1;
    }

    return r;
}

int aept_mark_manual(aept_ctx_t *ctx, const char **names, int count)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_mark_manual(ctx, names, count);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

static int api_mark_manual_all(aept_ctx_t *ctx)
{
    return aept_status_clear_auto(ctx);
}

int aept_mark_manual_all(aept_ctx_t *ctx)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_mark_manual_all(ctx);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

/* ── Query helpers ───────────────────────────────────────────────── */

/*
 * The three things every query has to agree on, so that "show", "show
 * -a" and "list" describe the same machine.
 *
 * They also have to agree with "install", which is the point: show
 * names a version somebody is about to install, and list marks what is
 * upgradable.  Both were computed as "highest version in the archive"
 * and so disagreed with the solver whenever a pin or an architecture
 * had a view.
 */

/*
 * Whether a source offers this solvable to this machine.
 * pool_installable() is libsolv's own test, so the architectures aept
 * accepts stay decided by pool_setarch() in aept_solver_init() rather
 * than restated at every query.  What is installed is never subject to
 * it: a package installed for an architecture since dropped from the
 * configuration is still on the disk.
 */
static int query_offered(Pool *pool, Solvable *s)
{
    return s->repo != pool->installed && pool_installable(pool, s);
}

/*
 * Which of two versions of one package a query should prefer, given the
 * version it is pinned to, or NULL.  A pin beats a higher version,
 * because the solver turns it into an exact solvable rather than a
 * preference; otherwise the higher version wins.
 */
static Solvable *query_prefer(Pool *pool, Solvable *best, Solvable *s, const char *pin_ver)
{
    if (pin_ver) {
        int s_pinned = strcmp(pool_id2str(pool, s->evr), pin_ver) == 0;
        int b_pinned = best && strcmp(pool_id2str(pool, best->evr), pin_ver) == 0;

        if (s_pinned != b_pinned)
            return s_pinned ? s : best;
    }

    if (!best)
        return s;

    return pool_evrcmp_str(pool, pool_id2str(pool, s->evr), pool_id2str(pool, best->evr),
                           EVRCMP_COMPARE) > 0
               ? s
               : best;
}

static int query_load_repos(aept_ctx_t *ctx)
{
    int i;

    for (i = 0; i < ctx->config.nsources; i++) {
        char *list_path = NULL;
        FILE *fp;

        aept_asprintf(&list_path, "%s/%s", ctx->config.lists_dir, ctx->config.sources[i].name);

        fp = fopen(list_path, "r");
        if (!fp) {
            free(list_path);
            continue;
        }

        /* An expired index is skipped rather than aborting the query: a
         * listing that silently omits one source is bad, but the diagnostic
         * says which, and refusing to answer at all would be worse. */
        if (aept_index_check_expiry(list_path, ctx->config.sources[i].name,
                                    ctx->config.check_index_expiry) == 0)
            aept_solver_load_repo(ctx, ctx->config.sources[i].name, fp, i);

        fclose(fp);
        free(list_path);
    }

    return 0;
}

/*
 * The opening every query shares.  Pins are loaded here and not only by
 * install: a query that skips them answers with a version an install
 * would not take.
 */
static int query_begin(struct aept_ctx *ctx)
{
    if (aept_solver_init(ctx) < 0)
        return -1;

    aept_status_load(ctx);
    query_load_repos(ctx);
    aept_pin_load_into_solver(ctx);
    return 0;
}

/* ── Query: list ─────────────────────────────────────────────────── */

struct api_list_entry {
    Id name_id;
    const char *name; /* pool string, stable while the pool lives */
    Solvable *avail;
    Solvable *installed;
};

static struct api_list_entry *find_list_entry(struct api_list_entry *entries, int n, Id name_id)
{
    int i;
    for (i = 0; i < n; i++) {
        if (entries[i].name_id == name_id)
            return &entries[i];
    }
    return NULL;
}

/*
 * Compares resolved name strings rather than looking them up through a
 * pool.  The pool used to be passed via a file-scope variable, which
 * two threads listing different contexts would overwrite for each
 * other; resolving the name when the entry is built removes the need
 * for one, and saves a lookup per comparison besides.
 */
static int cmp_api_list_entry(const void *a, const void *b)
{
    const struct api_list_entry *ea = a;
    const struct api_list_entry *eb = b;
    return strcmp(ea->name, eb->name);
}

static int api_list(aept_ctx_t *ctx, const char *pattern, int filter_installed,
                    int filter_upgradable, aept_pkg_list_t *out)
{
    Pool *pool;
    Id p;
    Solvable *s;
    struct api_list_entry *entries = NULL;
    int nentries = 0, alloc = 0;
    int i, r = -1;

    memset(out, 0, sizeof(*out));

    if (query_begin(ctx) < 0)
        return -1;

    pool = aept_solver_pool(ctx->solver);

    FOR_POOL_SOLVABLES(p)
    {
        struct api_list_entry *e;

        s = pool_id2solvable(pool, p);

        /* Not a version this machine could install, so not one that
         * makes anything upgradable. */
        if (s->repo != pool->installed && !query_offered(pool, s))
            continue;

        e = find_list_entry(entries, nentries, s->name);

        if (!e) {
            if (nentries >= alloc) {
                alloc = alloc ? alloc * 2 : 256;
                entries = aept_realloc(entries, alloc * sizeof(*entries));
            }
            e = &entries[nentries++];
            e->name_id = s->name;
            e->name = pool_id2str(pool, s->name);
            e->avail = NULL;
            e->installed = NULL;
        }

        if (s->repo == pool->installed)
            e->installed = s;
        else
            e->avail =
                query_prefer(pool, e->avail, s, aept_solver_pin_version(ctx->solver, e->name));
    }

    qsort(entries, nentries, sizeof(*entries), cmp_api_list_entry);

    out->entries = calloc(nentries, sizeof(aept_pkg_entry_t));
    if (!out->entries && nentries > 0)
        goto cleanup;

    for (i = 0; i < nentries; i++) {
        struct api_list_entry *e = &entries[i];
        const char *name = e->name;
        const char *summary;
        Solvable *show;
        int upgradable;

        if (pattern && fnmatch(pattern, name, 0) != 0)
            continue;

        if (filter_installed && !e->installed)
            continue;

        /*
         * Upgradable means an upgrade would move this package, so it is
         * the same judgement "show" makes about the candidate: e->avail
         * already honours the pin, and a package pinned to what it has
         * is not upgradable however much newer the archive is.
         */
        upgradable = e->installed && e->avail &&
                     pool_evrcmp_str(pool, pool_id2str(pool, e->avail->evr),
                                     pool_id2str(pool, e->installed->evr), EVRCMP_COMPARE) > 0;

        if (filter_upgradable && !upgradable)
            continue;

        show = filter_installed ? e->installed : (e->avail ? e->avail : e->installed);

        aept_pkg_entry_t *pe = &out->entries[out->count++];
        pe->name = strdup(e->name);
        pe->version = strdup(pool_id2str(pool, show->evr));
        summary = solvable_lookup_str(show, SOLVABLE_SUMMARY);
        pe->summary = summary ? strdup(summary) : NULL;
        pe->installed = e->installed != NULL;
        pe->upgradable = upgradable;
    }

    r = 0;

cleanup:
    free(entries);
    aept_solver_fini(ctx);
    return r;
}

int aept_list(aept_ctx_t *ctx, const char *pattern, int filter_installed, int filter_upgradable,
              aept_pkg_list_t *out)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_list(ctx, pattern, filter_installed, filter_upgradable, out);
    AEPT_OOM_LEAVE(ctx);
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

/*
 * Fill one info from one solvable.  is_installed describes THIS version,
 * not the package: "aept show" prints the candidate, which is commonly
 * not the version on disk, and a Status line copied from the package
 * would claim the candidate was installed when it is not.
 */
/*
 * The file the solvable was read from: the status area for what is
 * installed, the source's index for what is offered.  query_load_repos()
 * names each repo after its source, and that name is the index's file
 * name, so a solvable always leads back to its own stanza.
 */
static char *stanza_path(struct aept_ctx *ctx, Pool *pool, Solvable *s)
{
    char *path = NULL;

    if (s->repo == pool->installed)
        aept_asprintf(&path, "%s/%s.control", ctx->config.info_dir, pool_id2str(pool, s->name));
    else if (s->repo && s->repo->name)
        aept_asprintf(&path, "%s/%s", ctx->config.lists_dir, s->repo->name);

    return path;
}

static void fill_info(struct aept_ctx *ctx, Pool *pool, Solvable *s, aept_pkg_info_t *out)
{
    const char *str;
    unsigned int medianr;
    char *path, *stanza = NULL;

    memset(out, 0, sizeof(*out));

    out->name = strdup(pool_id2str(pool, s->name));
    out->version = strdup(pool_id2str(pool, s->evr));
    out->architecture = strdup(pool_id2str(pool, s->arch));
    out->installed_size = solvable_lookup_num(s, SOLVABLE_INSTALLSIZE, 0);

    /*
     * The relationship fields come from the stanza, not the pool: what
     * the packager declared rather than what the solver made of it.
     * libsolv drops the parentheses from "libx (>= 1.0)", appends every
     * package's own "name = evr" to Provides, and files Debian's
     * Replaces under obsoletes -- where it lands as the Conflicts value
     * or nowhere at all.  Right for solving, wrong to print.
     *
     * A stanza that cannot be found leaves them NULL, and the field is
     * simply not printed; that beats printing something else's value.
     */
    path = stanza_path(ctx, pool, s);
    if (path) {
        stanza = aept_stanza_find(path, out->name, out->version);
        free(path);
    }
    if (stanza) {
        char *num;

        out->section = aept_stanza_field(stanza, "Section");
        out->source = aept_stanza_field(stanza, "Source");
        out->maintainer = aept_stanza_field(stanza, "Maintainer");

        /* Size is the compressed archive, in bytes, and only an index
         * carries it: what is installed was downloaded long ago. */
        num = aept_stanza_field(stanza, "Size");
        if (num) {
            out->download_size = strtoull(num, NULL, 10);
            free(num);
        }

        out->depends = aept_stanza_field(stanza, "Depends");
        out->pre_depends = aept_stanza_field(stanza, "Pre-Depends");
        out->recommends = aept_stanza_field(stanza, "Recommends");
        out->suggests = aept_stanza_field(stanza, "Suggests");
        out->provides = aept_stanza_field(stanza, "Provides");
        out->conflicts = aept_stanza_field(stanza, "Conflicts");
        out->replaces = aept_stanza_field(stanza, "Replaces");
        free(stanza);
    }

    str = solvable_lookup_str(s, SOLVABLE_URL);
    out->homepage = str ? strdup(str) : NULL;

    str = solvable_lookup_location(s, &medianr);
    out->filename = str ? strdup(str) : NULL;

    str = solvable_lookup_str(s, SOLVABLE_SUMMARY);
    out->summary = str ? strdup(str) : NULL;

    str = solvable_lookup_str(s, SOLVABLE_DESCRIPTION);
    out->description = str ? strdup(str) : NULL;

    out->is_installed = (s->repo == pool->installed);
}

static int api_show(aept_ctx_t *ctx, const char *name, aept_pkg_info_t *out)
{
    Pool *pool;
    Id name_id, p;
    Solvable *s, *best = NULL, *installed = NULL;
    const char *pin_ver;
    int r = -1;

    memset(out, 0, sizeof(*out));

    if (query_begin(ctx) < 0)
        return -1;

    pool = aept_solver_pool(ctx->solver);

    name_id = pool_str2id(pool, name, 0);
    if (!name_id)
        goto not_found;

    pin_ver = aept_solver_pin_version(ctx->solver, name);

    FOR_POOL_SOLVABLES(p)
    {
        s = pool_id2solvable(pool, p);
        if (s->name != name_id)
            continue;

        if (s->repo == pool->installed)
            installed = s;
        else if (query_offered(pool, s))
            best = query_prefer(pool, best, s, pin_ver);
    }

    /*
     * A pin for a version no source offers falls back to best available,
     * as an install does -- with the same warning, since a pin that
     * silently does nothing is how a machine ends up on a version
     * somebody pinned away from.
     */
    if (pin_ver && best && strcmp(pool_id2str(pool, best->evr), pin_ver) != 0)
        aept_log_warning("pinned version '%s' of '%s' not found in any repository, "
                         "showing best available",
                         pin_ver, name);

    s = best ? best : installed;
    if (!s)
        goto not_found;

    fill_info(ctx, pool, s, out);

    r = 0;
    goto cleanup;

not_found:
    r = 1;
cleanup:
    aept_solver_fini(ctx);
    return r;
}

/*
 * Every version of the package, newest first: what is installed and what
 * each configured source offers.  A version present both on disk and in
 * a source appears once, as the installed one, since that is the copy
 * the machine actually has.
 */
static int api_show_all(aept_ctx_t *ctx, const char *name, aept_pkg_info_list_t *out)
{
    Pool *pool;
    Id name_id, p;
    Solvable *s;
    int r = -1, cap = 0, i;

    memset(out, 0, sizeof(*out));

    if (query_begin(ctx) < 0)
        return -1;

    pool = aept_solver_pool(ctx->solver);

    name_id = pool_str2id(pool, name, 0);
    if (!name_id)
        goto not_found;

    FOR_POOL_SOLVABLES(p)
    {
        const char *evr;
        int dup = 0;

        s = pool_id2solvable(pool, p);
        if (s->name != name_id)
            continue;

        /* A version this machine could not install is not one of its
         * versions, so it is not listed either. */
        if (s->repo != pool->installed && !query_offered(pool, s))
            continue;

        evr = pool_id2str(pool, s->evr);
        for (i = 0; i < out->count; i++) {
            if (strcmp(out->entries[i].version, evr) != 0)
                continue;
            dup = 1;
            /* An installed copy displaces the source's record of the
             * same version: the Status line is the useful difference. */
            if (s->repo == pool->installed) {
                aept_pkg_info_free(&out->entries[i]);
                fill_info(ctx, pool, s, &out->entries[i]);
            }
            break;
        }
        if (dup)
            continue;

        if (out->count == cap) {
            cap = cap ? cap * 2 : 4;
            out->entries = aept_realloc(out->entries, (size_t)cap * sizeof(*out->entries));
        }
        fill_info(ctx, pool, s, &out->entries[out->count++]);
    }

    if (out->count == 0)
        goto not_found;

    /*
     * Newest first, so the candidate leads and "show -a" reads like a
     * history.  Ordered by libsolv rather than by strcmp: version
     * strings do not compare as text -- "10.0" sorts before "9.0" that
     * way.  A selection sort because a package has a handful of
     * versions, not thousands.
     */
    for (i = 0; i < out->count; i++) {
        int k, top = i;

        for (k = i + 1; k < out->count; k++) {
            if (pool_evrcmp_str(pool, out->entries[k].version, out->entries[top].version,
                                EVRCMP_COMPARE) > 0)
                top = k;
        }
        if (top != i) {
            aept_pkg_info_t tmp = out->entries[i];
            out->entries[i] = out->entries[top];
            out->entries[top] = tmp;
        }
    }

    r = 0;
    goto cleanup;

not_found:
    r = 1;
cleanup:
    aept_solver_fini(ctx);
    return r;
}

void aept_pkg_info_list_free(aept_pkg_info_list_t *list)
{
    int i;

    if (!list || !list->entries)
        return;

    for (i = 0; i < list->count; i++)
        aept_pkg_info_free(&list->entries[i]);
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
}

int aept_show(aept_ctx_t *ctx, const char *name, aept_pkg_info_t *out)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_show(ctx, name, out);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

int aept_show_all(aept_ctx_t *ctx, const char *name, aept_pkg_info_list_t *out)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_show_all(ctx, name, out);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

void aept_pkg_info_free(aept_pkg_info_t *info)
{
    if (!info)
        return;

    free(info->name);
    free(info->version);
    free(info->architecture);
    free(info->section);
    free(info->source);
    free(info->maintainer);
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

static int api_files(aept_ctx_t *ctx, const char *name, char ***paths_out, int *count_out)
{
    char *list_path = NULL;
    FILE *fp;
    char buf[4096];
    char **paths = NULL;
    int count = 0, alloc = 0;

    *paths_out = NULL;
    *count_out = 0;

    if (!aept_pkg_name_is_safe(name))
        return -1;

    aept_asprintf(&list_path, "%s/%s.list", ctx->config.info_dir, name);

    fp = fopen(list_path, "r");
    free(list_path);

    if (!fp)
        return 1;

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
                return -1;
            }
        }
        paths[count++] = strdup(buf);
    }

    fclose(fp);

    *paths_out = paths;
    *count_out = count;
    return 0;
}

int aept_files(aept_ctx_t *ctx, const char *name, char ***paths_out, int *count_out)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_files(ctx, name, paths_out, count_out);
    AEPT_OOM_LEAVE(ctx);
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

static int api_owns(aept_ctx_t *ctx, const char *path, char ***owners_out, int *count_out)
{
    DIR *dir;
    struct dirent *ent;
    const char *needle;
    size_t needle_len;
    char **owners = NULL;
    int count = 0, alloc = 0;

    *owners_out = NULL;
    *count_out = 0;

    if (!path || *path == '\0')
        return -1;

    needle = strip_leading(path);
    if (*needle == '\0')
        needle = ".";
    needle_len = owns_path_len(needle);

    dir = opendir(ctx->config.info_dir);
    if (!dir)
        return 1;

    while ((ent = readdir(dir)) != NULL) {
        const char *dot;
        char *list_path = NULL;
        FILE *fp;
        char buf[4096];

        dot = strrchr(ent->d_name, '.');
        if (!dot || strcmp(dot, ".list") != 0)
            continue;

        aept_asprintf(&list_path, "%s/%s", ctx->config.info_dir, ent->d_name);
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

            if (owns_path_len(entry) == needle_len && strncmp(entry, needle, needle_len) == 0) {
                size_t name_len = (size_t)(dot - ent->d_name);

                if (count >= alloc) {
                    alloc = alloc ? alloc * 2 : 4;
                    owners = aept_realloc(owners, alloc * sizeof(char *));
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
    return count > 0 ? 0 : 1;
}

int aept_owns(aept_ctx_t *ctx, const char *path, char ***owners_out, int *count_out)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_owns(ctx, path, owners_out, count_out);
    AEPT_OOM_LEAVE(ctx);
    return r;
}

/* ── Query: architectures ────────────────────────────────────────── */

static int api_architectures(aept_ctx_t *ctx, char ***archs_out, int *count_out)
{
    int i;

    *archs_out = NULL;
    *count_out = 0;

    if (ctx->config.narchs > 0) {
        char **archs = malloc(ctx->config.narchs * sizeof(char *));
        if (!archs)
            return -1;

        for (i = 0; i < ctx->config.narchs; i++)
            archs[i] = strdup(ctx->config.archs[i]);

        *archs_out = archs;
        *count_out = ctx->config.narchs;
    }

    return 0;
}

int aept_architectures(aept_ctx_t *ctx, char ***archs_out, int *count_out)
{
    int r;
    AEPT_OOM_ENTER(ctx, -1);

    r = api_architectures(ctx, archs_out, count_out);
    AEPT_OOM_LEAVE(ctx);
    return r;
}
