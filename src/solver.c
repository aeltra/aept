/* solver.c - libsolv integration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#include "aept/aept.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/util.h"

#define MAX_REPOS 64

static Pool *pool;
static Repo *installed_repo;
static Repo *repos[MAX_REPOS];
static int repo_source_index[MAX_REPOS];
static int nrepos;
static Solver *solv;
static Transaction *trans;

/* Commandline repo for local .ipk files */
static Repo *commandline_repo;

#define MAX_CMDLINE 256

typedef struct {
    Id id;
    char *path;
} cmdline_entry_t;

static cmdline_entry_t cmdline_entries[MAX_CMDLINE];
static int ncmdline;

typedef struct {
    char *name;
    char *version;
} pin_entry_t;

static pin_entry_t *pins;
static int npins;

int solver_init(void)
{
    pool = pool_create();
    if (!pool) {
        log_error("failed to create solver pool");
        return -1;
    }

    if (cfg->narchs > 0) {
        int i;
        size_t len = 0;
        char *archstr;

        for (i = 0; i < cfg->narchs; i++)
            len += strlen(cfg->archs[i]) + 1;

        archstr = xmalloc(len);
        archstr[0] = '\0';

        for (i = 0; i < cfg->narchs; i++) {
            if (i > 0)
                strcat(archstr, ":");
            strcat(archstr, cfg->archs[i]);
        }

        pool_setarch(pool, archstr);
        free(archstr);
    } else {
        pool_setarch(pool, "noarch");
    }

    installed_repo = NULL;
    commandline_repo = NULL;
    nrepos = 0;
    ncmdline = 0;
    solv = NULL;
    trans = NULL;

    return 0;
}

int solver_load_repo(const char *name, FILE *fp, int source_index)
{
    Repo *repo;

    if (nrepos >= MAX_REPOS) {
        log_error("too many repositories");
        return -1;
    }

    repo = repo_create(pool, name);
    if (!repo) {
        log_error("failed to create repo '%s'", name);
        return -1;
    }

    if (repo_add_debpackages(repo, fp, 0)) {
        log_error("failed to parse Packages for '%s'", name);
        repo_free(repo, 0);
        return -1;
    }

    repos[nrepos] = repo;
    repo_source_index[nrepos] = source_index;
    nrepos++;

    return 0;
}

int solver_load_installed(FILE *fp)
{
    installed_repo = repo_create(pool, "@installed");
    if (!installed_repo) {
        log_error("failed to create installed repo");
        return -1;
    }

    if (repo_add_debpackages(installed_repo, fp, 0)) {
        log_error("failed to parse status file");
        repo_free(installed_repo, 0);
        installed_repo = NULL;
        return -1;
    }

    pool_set_installed(pool, installed_repo);

    return 0;
}

Id solver_load_local(const char *path)
{
    Id p;

    if (ncmdline >= MAX_CMDLINE) {
        log_error("too many local packages");
        return 0;
    }

    if (!commandline_repo) {
        commandline_repo = repo_create(pool, "@commandline");
        if (!commandline_repo) {
            log_error("failed to create commandline repo");
            return 0;
        }
    }

    p = repo_add_deb(commandline_repo, path, 0);
    if (!p) {
        log_error("failed to read '%s'", path);
        return 0;
    }

    cmdline_entries[ncmdline].id = p;
    cmdline_entries[ncmdline].path = xstrdup(path);
    ncmdline++;

    return p;
}

int solver_is_commandline(Id p)
{
    Solvable *s;

    if (!commandline_repo)
        return 0;

    s = pool_id2solvable(pool, p);
    return s->repo == commandline_repo;
}

const char *solver_commandline_path(Id p)
{
    int i;

    for (i = 0; i < ncmdline; i++) {
        if (cmdline_entries[i].id == p)
            return cmdline_entries[i].path;
    }

    return NULL;
}

static int do_solve(Queue *job)
{
    int problems;
    Id problem;

    pool_createwhatprovides(pool);

    solv = solver_create(pool);
    if (!solv) {
        log_error("failed to create solver");
        return -1;
    }

    solver_set_flag(solv, SOLVER_FLAG_ALLOW_UNINSTALL, 1);
    solver_set_flag(solv, SOLVER_FLAG_ALLOW_ARCHCHANGE, 1);

    if (cfg->allow_downgrade)
        solver_set_flag(solv, SOLVER_FLAG_ALLOW_DOWNGRADE, 1);

    problems = solver_solve(solv, job);
    if (problems > 0 && cfg->force_depends) {
        log_warning("dependency problems (--force-depends, accepting solutions):");

        problem = 0;
        while ((problem = solver_next_problem(solv, problem)) != 0) {
            Id solution;

            log_warning("  - %s", solver_problem2str(solv, problem));

            solution = solver_next_solution(solv, problem, 0);
            if (solution)
                solver_take_solution(solv, problem, solution, job);
        }

        problems = solver_solve(solv, job);
    }

    if (problems > 0) {
        log_error("dependency problems:");

        problem = 0;
        while ((problem = solver_next_problem(solv, problem)) != 0) {
            const char *s = solver_problem2str(solv, problem);
            log_error("  - %s", s);
        }

        solver_free(solv);
        solv = NULL;
        return -1;
    }

    trans = solver_create_transaction(solv);
    transaction_order(trans, 0);

    return 0;
}

static const char *find_pin_version(const char *name)
{
    int i;

    for (i = 0; i < npins; i++) {
        if (strcmp(name, pins[i].name) == 0)
            return pins[i].version;
    }

    return NULL;
}

int solver_resolve_install(const char **names, int count,
                           const Id *local_ids, int local_count)
{
    Queue job;
    int i, r;

    queue_init(&job);

    if ((names == NULL || count == 0) &&
            (local_ids == NULL || local_count == 0)) {
        /* upgrade all */
        queue_push2(&job, SOLVER_UPDATE | SOLVER_SOLVABLE_ALL, 0);

        /* lock pinned packages to prevent upgrade */
        for (i = 0; i < npins; i++) {
            Id nameid = pool_str2id(pool, pins[i].name, 0);
            if (nameid)
                queue_push2(&job,
                            SOLVER_LOCK | SOLVER_SOLVABLE_NAME, nameid);
        }
    } else {
        for (i = 0; i < count; i++) {
            const char *pin_ver = find_pin_version(names[i]);

            if (pin_ver) {
                Id nameid = pool_str2id(pool, names[i], 0);
                Id target = 0;

                if (nameid) {
                    Id p, pp;
                    FOR_PROVIDES(p, pp, nameid) {
                        Solvable *s = pool_id2solvable(pool, p);
                        if (s->repo == pool->installed)
                            continue;
                        if (pool_evrcmp_str(pool,
                                pool_id2str(pool, s->evr),
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
                    log_warning("pinned version '%s' of '%s' not found "
                                "in any repository, installing best "
                                "available", pin_ver, names[i]);
                    Id id = pool_str2id(pool, names[i], 1);
                    queue_push2(&job,
                                SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES,
                                id);
                }
            } else {
                Id id = pool_str2id(pool, names[i], 1);
                queue_push2(&job,
                            SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES, id);
            }
        }

        for (i = 0; i < local_count; i++)
            queue_push2(&job,
                        SOLVER_INSTALL | SOLVER_SOLVABLE, local_ids[i]);
    }

    r = do_solve(&job);
    queue_free(&job);

    return r;
}

int solver_resolve_remove(const char **names, int count)
{
    Queue job;
    int i, r;

    queue_init(&job);

    for (i = 0; i < count; i++) {
        Id id = pool_str2id(pool, names[i], 1);
        queue_push2(&job, SOLVER_ERASE | SOLVER_SOLVABLE_PROVIDES, id);
    }

    r = do_solve(&job);
    queue_free(&job);

    return r;
}

Transaction *solver_transaction(void)
{
    return trans;
}

Pool *solver_pool(void)
{
    return pool;
}

int solver_solvable_source_index(Id p)
{
    Solvable *s = pool_id2solvable(pool, p);
    int i;

    for (i = 0; i < nrepos; i++) {
        if (s->repo == repos[i])
            return repo_source_index[i];
    }

    return -1;
}

Id solver_find_available(const char *name)
{
    Id nameid = pool_str2id(pool, name, 0);
    Id best = 0;
    Id p, pp;

    if (!nameid)
        return 0;

    FOR_PROVIDES(p, pp, nameid) {
        Solvable *s = pool_id2solvable(pool, p);

        if (s->repo == pool->installed)
            continue;

        if (!best ||
                pool_evrcmp(pool,
                    pool_id2solvable(pool, best)->evr,
                    s->evr, EVRCMP_COMPARE) < 0) {
            best = p;
        }
    }

    return best;
}

void solver_add_pin(const char *name, const char *version)
{
    npins++;
    pins = xrealloc(pins, npins * sizeof(pin_entry_t));
    pins[npins - 1].name = xstrdup(name);
    pins[npins - 1].version = xstrdup(version);
}

void solver_clear_pins(void)
{
    int i;

    for (i = 0; i < npins; i++) {
        free(pins[i].name);
        free(pins[i].version);
    }
    free(pins);
    pins = NULL;
    npins = 0;
}

void solver_fini(void)
{
    int i;

    solver_clear_pins();

    if (trans) {
        transaction_free(trans);
        trans = NULL;
    }

    if (solv) {
        solver_free(solv);
        solv = NULL;
    }

    if (pool) {
        pool_free(pool);
        pool = NULL;
    }

    for (i = 0; i < ncmdline; i++)
        free(cmdline_entries[i].path);
    ncmdline = 0;
    commandline_repo = NULL;

    installed_repo = NULL;
    nrepos = 0;
}
