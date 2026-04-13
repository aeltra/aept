/* solver.c - libsolv integration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <solv/evr.h>
#include <solv/pool.h>
#include <solv/poolarch.h>
#include <solv/repo.h>
#include <solv/repo_deb.h>
#include <solv/solver.h>
#include <solv/solvable.h>
#include <solv/transaction.h>
#include <solv/queue.h>
#include <solv/problems.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/util.h"

int aept_solver_init(struct aept_ctx *ctx)
{
    aept_solver_t *s = aept_malloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    s->pool = pool_create();
    if (!s->pool) {
        aept_log_error("failed to create solver pool");
        free(s);
        return -1;
    }

    if (ctx->config.narchs > 0) {
        int i;
        size_t len = 0;
        char *archstr;

        for (i = 0; i < ctx->config.narchs; i++)
            len += strlen(ctx->config.archs[i]) + 1;

        archstr = aept_malloc(len);
        archstr[0] = '\0';

        for (i = 0; i < ctx->config.narchs; i++) {
            if (i > 0)
                strcat(archstr, ":");
            strcat(archstr, ctx->config.archs[i]);
        }

        pool_setarch(s->pool, archstr);
        free(archstr);
    } else {
        pool_setarch(s->pool, "noarch");
    }

    ctx->solver = s;
    return 0;
}

int aept_solver_load_repo(struct aept_ctx *ctx, const char *name,
                           FILE *fp, int source_index)
{
    aept_solver_t *s = ctx->solver;
    Repo *repo;

    if (s->nrepos >= AEPT_MAX_REPOS) {
        aept_log_error("too many repositories");
        return -1;
    }

    repo = repo_create(s->pool, name);
    if (!repo) {
        aept_log_error("failed to create repo '%s'", name);
        return -1;
    }

    if (repo_add_debpackages(repo, fp, 0)) {
        aept_log_error("failed to parse Packages for '%s'", name);
        repo_free(repo, 0);
        return -1;
    }

    s->repos[s->nrepos] = repo;
    s->repo_source_index[s->nrepos] = source_index;
    s->nrepos++;

    return 0;
}

int aept_solver_load_installed(struct aept_ctx *ctx, FILE *fp)
{
    aept_solver_t *s = ctx->solver;

    s->installed_repo = repo_create(s->pool, "@installed");
    if (!s->installed_repo) {
        aept_log_error("failed to create installed repo");
        return -1;
    }

    if (repo_add_debpackages(s->installed_repo, fp, 0)) {
        aept_log_error("failed to parse status file");
        repo_free(s->installed_repo, 0);
        s->installed_repo = NULL;
        return -1;
    }

    pool_set_installed(s->pool, s->installed_repo);

    return 0;
}

Id aept_solver_load_local(struct aept_ctx *ctx, const char *path)
{
    aept_solver_t *s = ctx->solver;
    Id p;

    if (s->ncmdline >= AEPT_MAX_CMDLINE) {
        aept_log_error("too many local packages");
        return 0;
    }

    if (!s->commandline_repo) {
        s->commandline_repo = repo_create(s->pool, "@commandline");
        if (!s->commandline_repo) {
            aept_log_error("failed to create commandline repo");
            return 0;
        }
    }

    p = repo_add_deb(s->commandline_repo, path, 0);
    if (!p) {
        aept_log_error("failed to read '%s'", path);
        return 0;
    }

    s->cmdline_entries[s->ncmdline].id = p;
    s->cmdline_entries[s->ncmdline].path = aept_strdup(path);
    s->ncmdline++;

    return p;
}

int aept_solver_is_commandline(aept_solver_t *s, Id p)
{
    Solvable *sv;

    if (!s->commandline_repo)
        return 0;

    sv = pool_id2solvable(s->pool, p);
    return sv->repo == s->commandline_repo;
}

const char *aept_solver_commandline_path(aept_solver_t *s, Id p)
{
    int i;

    for (i = 0; i < s->ncmdline; i++) {
        if (s->cmdline_entries[i].id == p)
            return s->cmdline_entries[i].path;
    }

    return NULL;
}

static int do_solve(struct aept_ctx *ctx, Queue *job, int keep_orderdata)
{
    aept_solver_t *s = ctx->solver;
    int problems;
    Id problem;

    pool_createwhatprovides(s->pool);

    s->solv = solver_create(s->pool);
    if (!s->solv) {
        aept_log_error("failed to create solver");
        return -1;
    }

    solver_set_flag(s->solv, SOLVER_FLAG_ALLOW_UNINSTALL, 1);
    solver_set_flag(s->solv, SOLVER_FLAG_ALLOW_ARCHCHANGE, 1);

    if (ctx->config.allow_downgrade)
        solver_set_flag(s->solv, SOLVER_FLAG_ALLOW_DOWNGRADE, 1);

    problems = solver_solve(s->solv, job);
    if (problems > 0 && ctx->config.force_depends) {
        aept_log_warning("dependency problems (--force-depends, accepting solutions):");

        problem = 0;
        while ((problem = solver_next_problem(s->solv, problem)) != 0) {
            Id solution;

            aept_log_warning("  - %s", solver_problem2str(s->solv, problem));

            solution = solver_next_solution(s->solv, problem, 0);
            if (solution)
                solver_take_solution(s->solv, problem, solution, job);
        }

        problems = solver_solve(s->solv, job);
    }

    if (problems > 0) {
        aept_log_error("dependency problems:");

        problem = 0;
        while ((problem = solver_next_problem(s->solv, problem)) != 0) {
            const char *str = solver_problem2str(s->solv, problem);
            aept_log_error("  - %s", str);
        }

        solver_free(s->solv);
        s->solv = NULL;
        return -1;
    }

    s->trans = solver_create_transaction(s->solv);
    transaction_order(s->trans,
        keep_orderdata ? SOLVER_TRANSACTION_KEEP_ORDERDATA : 0);

    return 0;
}

static const char *find_pin_version(aept_solver_t *s, const char *name)
{
    int i;

    for (i = 0; i < s->npins; i++) {
        if (strcmp(name, s->pins[i].name) == 0)
            return s->pins[i].version;
    }

    return NULL;
}

/*
 * Reorder trans->steps so that user-listed packages are installed in
 * the order they were specified on the command line, pulling in their
 * dependencies just before each one.  Uses libsolv's "roll your own"
 * ordering API (transaction_order_add_choices) which returns the set
 * of packages whose dependencies are already satisfied at each step.
 */
static void reorder_transaction(Transaction *trans, Pool *pool,
                                const char **names, int name_count,
                                const Id *local_ids, int local_count)
{
    Queue choices, ordered;
    Id chosen, *user_ids;
    int i, j, total, best_idx;
    int next_user = 0;
    int user_count = name_count + local_count;

    total = trans->steps.count;

    /* Resolve user-listed names to solvable IDs once up front so
     * the inner loops only do integer comparisons. */
    user_ids = aept_malloc(user_count * sizeof(Id));

    for (i = 0; i < name_count; i++) {
        user_ids[i] = 0;
        for (j = 0; j < total; j++) {
            Id p = trans->steps.elements[j];
            Solvable *s = pool_id2solvable(pool, p);
            if (strcmp(pool_id2str(pool, s->name), names[i]) == 0) {
                user_ids[i] = p;
                break;
            }
        }
    }
    for (i = 0; i < local_count; i++)
        user_ids[name_count + i] = local_ids[i];

    queue_init(&choices);
    queue_init(&ordered);

    /* Seed with packages that have no unsatisfied dependencies */
    transaction_order_add_choices(trans, 0, &choices);

    while (choices.count > 0) {
        best_idx = -1;

        /* Skip user entries that could not be resolved */
        while (next_user < user_count && !user_ids[next_user])
            next_user++;

        /* Pick the next user-listed package if it is available */
        if (next_user < user_count) {
            for (i = 0; i < choices.count; i++) {
                if (choices.elements[i] == user_ids[next_user]) {
                    best_idx = i;
                    next_user++;
                    break;
                }
            }
        }

        /* Otherwise pick the first dependency (non-user-listed) */
        if (best_idx < 0) {
            for (i = 0; i < choices.count; i++) {
                int is_user = 0;
                for (j = next_user; j < user_count; j++) {
                    if (user_ids[j] && choices.elements[i] == user_ids[j]) {
                        is_user = 1;
                        break;
                    }
                }
                if (!is_user) {
                    best_idx = i;
                    break;
                }
            }
        }

        /* Fallback: only later user-listed packages available */
        if (best_idx < 0)
            best_idx = 0;

        chosen = choices.elements[best_idx];
        queue_push(&ordered, chosen);
        queue_delete(&choices, best_idx);

        /* Unlock packages that depended on chosen */
        transaction_order_add_choices(trans, chosen, &choices);
    }

    /*
     * If a dependency cycle prevented transaction_order_add_choices
     * from releasing all packages, fall back to the original ordering
     * produced by transaction_order rather than silently dropping
     * cycle members.
     */
    aept_log_warning("reorder: %d of %d steps", ordered.count, total);

    if (ordered.count == total) {
        queue_empty(&trans->steps);
        for (i = 0; i < ordered.count; i++)
            queue_push(&trans->steps, ordered.elements[i]);
    }

    queue_free(&choices);
    queue_free(&ordered);
    free(user_ids);
    transaction_free_orderdata(trans);
}

int aept_solver_resolve_install(struct aept_ctx *ctx,
                           const char **names, int count,
                           const Id *local_ids, int local_count)
{
    aept_solver_t *s = ctx->solver;
    Pool *pool = s->pool;
    Queue job;
    int i, r;

    queue_init(&job);

    if ((names == NULL || count == 0) &&
            (local_ids == NULL || local_count == 0)) {
        /* upgrade all */
        queue_push2(&job, SOLVER_UPDATE | SOLVER_SOLVABLE_ALL, 0);

        /* lock pinned packages to prevent upgrade */
        for (i = 0; i < s->npins; i++) {
            Id nameid = pool_str2id(s->pool, s->pins[i].name, 0);
            if (nameid)
                queue_push2(&job,
                            SOLVER_LOCK | SOLVER_SOLVABLE_NAME, nameid);
        }
    } else {
        for (i = 0; i < count; i++) {
            const char *pin_ver = find_pin_version(s, names[i]);

            if (pin_ver) {
                Id nameid = pool_str2id(s->pool, names[i], 0);
                Id target = 0;

                if (nameid) {
                    Id p, pp;
                    FOR_PROVIDES(p, pp, nameid) {
                        Solvable *sv = pool_id2solvable(s->pool, p);
                        if (sv->repo == s->pool->installed)
                            continue;
                        if (pool_evrcmp_str(s->pool,
                                pool_id2str(s->pool, sv->evr),
                                pin_ver, EVRCMP_COMPARE) == 0) {
                            target = p;
                            break;
                        }
                    }
                }

                if (target) {
                    queue_push2(&job,
                                SOLVER_INSTALL | SOLVER_SOLVABLE, target);
                } else {
                    aept_log_warning("pinned version '%s' of '%s' not found "
                                "in any repository, installing best "
                                "available", pin_ver, names[i]);
                    Id id = pool_str2id(s->pool, names[i], 1);
                    queue_push2(&job,
                                SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES,
                                id);
                }
            } else {
                Id id = pool_str2id(s->pool, names[i], 1);
                queue_push2(&job,
                            SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES, id);
            }
        }

        for (i = 0; i < local_count; i++)
            queue_push2(&job,
                        SOLVER_INSTALL | SOLVER_SOLVABLE, local_ids[i]);
    }

    r = do_solve(ctx, &job, count > 0 || local_count > 0);
    if (r == 0 && (count > 0 || local_count > 0))
        reorder_transaction(s->trans, s->pool, names, count,
                            local_ids, local_count);

    queue_free(&job);

    return r;
}

int aept_solver_resolve_remove(struct aept_ctx *ctx,
                                const char **names, int count)
{
    aept_solver_t *s = ctx->solver;
    Queue job;
    int i, r;

    queue_init(&job);

    for (i = 0; i < count; i++) {
        Id id = pool_str2id(s->pool, names[i], 1);
        queue_push2(&job, SOLVER_ERASE | SOLVER_SOLVABLE_PROVIDES, id);
    }

    r = do_solve(ctx, &job, 0);
    queue_free(&job);

    return r;
}

Transaction *aept_solver_transaction(aept_solver_t *s)
{
    return s->trans;
}

Pool *aept_solver_pool(aept_solver_t *s)
{
    return s->pool;
}

int aept_solver_solvable_source_index(aept_solver_t *s, Id p)
{
    Solvable *sv = pool_id2solvable(s->pool, p);
    int i;

    for (i = 0; i < s->nrepos; i++) {
        if (sv->repo == s->repos[i])
            return s->repo_source_index[i];
    }

    return -1;
}

Id aept_solver_find_available(aept_solver_t *s, const char *name)
{
    Pool *pool = s->pool;
    Id nameid = pool_str2id(pool, name, 0);
    Id best = 0;
    Id p, pp;

    if (!nameid)
        return 0;

    FOR_PROVIDES(p, pp, nameid) {
        Solvable *sv = pool_id2solvable(pool, p);

        if (sv->repo == pool->installed)
            continue;

        if (!best ||
                pool_evrcmp(pool,
                    pool_id2solvable(pool, best)->evr,
                    sv->evr, EVRCMP_COMPARE) < 0) {
            best = p;
        }
    }

    return best;
}

void aept_solver_add_pin(aept_solver_t *s, const char *name,
                          const char *version)
{
    s->npins++;
    s->pins = aept_realloc(s->pins, s->npins * sizeof(aept_pin_entry_t));
    s->pins[s->npins - 1].name = aept_strdup(name);
    s->pins[s->npins - 1].version = aept_strdup(version);
}

void aept_solver_clear_pins(aept_solver_t *s)
{
    int i;

    for (i = 0; i < s->npins; i++) {
        free(s->pins[i].name);
        free(s->pins[i].version);
    }
    free(s->pins);
    s->pins = NULL;
    s->npins = 0;
}

const char *aept_solver_installed_version(aept_solver_t *s, const char *name)
{
    if (!s->pool || !s->pool->installed)
        return NULL;

    Id p;
    Solvable *sv;

    FOR_REPO_SOLVABLES(s->pool->installed, p, sv) {
        if (strcmp(pool_id2str(s->pool, sv->name), name) == 0)
            return pool_id2str(s->pool, sv->evr);
    }

    return NULL;
}

void aept_solver_fini(struct aept_ctx *ctx)
{
    aept_solver_t *s = ctx->solver;
    int i;

    if (!s)
        return;

    aept_solver_clear_pins(s);

    if (s->trans) {
        transaction_free(s->trans);
        s->trans = NULL;
    }

    if (s->solv) {
        solver_free(s->solv);
        s->solv = NULL;
    }

    if (s->pool) {
        pool_free(s->pool);
        s->pool = NULL;
    }

    for (i = 0; i < s->ncmdline; i++)
        free(s->cmdline_entries[i].path);

    free(s);
    ctx->solver = NULL;
}
