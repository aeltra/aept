/* solver.c - libsolv integration */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define MAX_REPOS 64

static Pool *pool;
static Repo *installed_repo;
static Repo *repos[MAX_REPOS];
static int repo_source_index[MAX_REPOS];
static int nrepos;
static Solver *solv;
static Transaction *trans;

int solver_init(void)
{
    pool = pool_create();
    if (!pool) {
        aept_msg(AEPT_ERROR, "failed to create solver pool\n");
        return -1;
    }

    if (cfg->narchs > 0)
        pool_setarch(pool, cfg->archs[0]);
    else
        pool_setarch(pool, "noarch");

    installed_repo = NULL;
    nrepos = 0;
    solv = NULL;
    trans = NULL;

    return 0;
}

int solver_load_repo(const char *name, FILE *fp, int source_index)
{
    Repo *repo;

    if (nrepos >= MAX_REPOS) {
        aept_msg(AEPT_ERROR, "too many repositories\n");
        return -1;
    }

    repo = repo_create(pool, name);
    if (!repo) {
        aept_msg(AEPT_ERROR, "failed to create repo '%s'\n", name);
        return -1;
    }

    if (repo_add_debpackages(repo, fp, 0)) {
        aept_msg(AEPT_ERROR, "failed to parse Packages for '%s'\n", name);
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
        aept_msg(AEPT_ERROR, "failed to create installed repo\n");
        return -1;
    }

    if (repo_add_debpackages(installed_repo, fp, 0)) {
        aept_msg(AEPT_ERROR, "failed to parse status file\n");
        repo_free(installed_repo, 0);
        installed_repo = NULL;
        return -1;
    }

    pool_set_installed(pool, installed_repo);

    return 0;
}

static int do_solve(Queue *job)
{
    int problems;
    Id problem;

    pool_createwhatprovides(pool);

    solv = solver_create(pool);
    if (!solv) {
        aept_msg(AEPT_ERROR, "failed to create solver\n");
        return -1;
    }

    problems = solver_solve(solv, job);
    if (problems > 0) {
        aept_msg(AEPT_ERROR, "dependency problems:\n");

        problem = 0;
        while ((problem = solver_next_problem(solv, problem)) != 0) {
            const char *s = solver_problem2str(solv, problem);
            aept_msg(AEPT_ERROR, "  - %s\n", s);
        }

        solver_free(solv);
        solv = NULL;
        return -1;
    }

    trans = solver_create_transaction(solv);
    transaction_order(trans, 0);

    return 0;
}

int solver_resolve_install(const char **names, int count)
{
    Queue job;
    int i, r;

    queue_init(&job);

    if (names == NULL || count == 0) {
        /* upgrade all */
        queue_push2(&job, SOLVER_UPDATE | SOLVER_SOLVABLE_ALL, 0);
    } else {
        for (i = 0; i < count; i++) {
            Id id = pool_str2id(pool, names[i], 1);
            queue_push2(&job, SOLVER_INSTALL | SOLVER_SOLVABLE_NAME, id);
        }
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
        queue_push2(&job, SOLVER_ERASE | SOLVER_SOLVABLE_NAME, id);
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

void solver_fini(void)
{
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

    installed_repo = NULL;
    nrepos = 0;
}
