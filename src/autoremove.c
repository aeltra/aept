/* autoremove.c - remove unneeded auto-installed packages
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/repo.h>
#include <solv/solvable.h>

#include "aept/aept.h"
#include "aept/internal.h"
#include "aept/autoremove.h"
#include "aept/msg.h"
#include "aept/remove.h"
#include "aept/solver.h"
#include "aept/status.h"
#include "aept/trigger.h"
#include "aept/util.h"

/* Mark solvable p as needed and recursively mark its dependencies. */
static void mark_needed(Pool *pool, Id p, char *needed, int ninstalled)
{
    Solvable *s = pool_id2solvable(pool, p);
    Id *reqp, req;
    Id p2, pp2;

    if (p < pool->installed->start || p >= pool->installed->end)
        return;

    int idx = p - pool->installed->start;
    if (idx < 0 || idx >= ninstalled)
        return;

    if (needed[idx])
        return;

    needed[idx] = 1;

    if (!s->requires)
        return;

    reqp = s->repo->idarraydata + s->requires;
    while ((req = *reqp++) != 0) {
        if (req == SOLVABLE_PREREQMARKER)
            continue;

        FOR_PROVIDES(p2, pp2, req) {
            Solvable *s2 = pool_id2solvable(pool, p2);
            if (s2->repo == pool->installed)
                mark_needed(pool, p2, needed, ninstalled);
        }
    }
}

int aept_op_autoremove(struct aept_ctx *ctx)
{
    Pool *pool;
    Repo *installed;
    aept_fileset_t auto_set;
    char *needed = NULL;
    const char **candidates = NULL;
    const char **candidates_evr = NULL;
    int ncandidates = 0;
    int ninstalled;
    Id p;
    Solvable *s;
    int i, r;

    r = aept_solver_init(ctx);
    if (r < 0)
        return -1;

    r = aept_status_load(ctx);
    if (r < 0)
        goto out;

    pool = aept_solver_pool(ctx->solver);
    installed = pool->installed;

    if (!installed || installed->end <= installed->start) {
        aept_log_info("nothing to do");
        r = 0;
        goto out;
    }

    pool_createwhatprovides(pool);

    aept_fileset_init(&auto_set);
    r = aept_status_load_auto_set(ctx, &auto_set);
    if (r < 0)
        goto out_fileset;

    if (auto_set.count == 0) {
        aept_log_info("nothing to do");
        r = 0;
        goto out_fileset;
    }

    /* BFS: mark all packages reachable from manually-installed ones. */
    ninstalled = installed->end - installed->start;
    needed = aept_malloc(ninstalled);
    memset(needed, 0, ninstalled);

    FOR_REPO_SOLVABLES(installed, p, s) {
        const char *name = pool_id2str(pool, s->name);
        if (!aept_fileset_contains(&auto_set, name))
            mark_needed(pool, p, needed, ninstalled);
    }

    /* Collect auto-installed packages that are not needed. */
    FOR_REPO_SOLVABLES(installed, p, s) {
        int idx = p - installed->start;
        if (idx < 0 || idx >= ninstalled)
            continue;
        if (needed[idx])
            continue;

        const char *name = pool_id2str(pool, s->name);
        if (!aept_fileset_contains(&auto_set, name))
            continue;

        ncandidates++;
        candidates = aept_realloc(candidates,
                              ncandidates * sizeof(const char *));
        candidates_evr = aept_realloc(candidates_evr,
                                  ncandidates * sizeof(const char *));
        candidates[ncandidates - 1] = name;
        candidates_evr[ncandidates - 1] = pool_id2str(pool, s->evr);
    }

    if (ncandidates == 0) {
        aept_log_info("nothing to do");
        r = 0;
        goto out_needed;
    }

    aept_transaction_t txn = {0};
    txn.remove  = candidates;
    txn.n_remove = ncandidates;

    aept_display_transaction(&txn);

    if (!aept_confirm_continue()) {
        r = 0;
        goto out_needed;
    }

    if (ctx->config.noaction) {
        aept_log_info("dry run, not removing");
        r = 0;
        goto out_needed;
    }

    aept_trigger_ctx_t tctx;
    aept_trigger_ctx_init(&tctx);

    int had_error = 0;

    for (i = 0; i < ncandidates; i++) {
        if (aept_cancelled()) {
            aept_log_warning("interrupted, stopping");
            r = -1;
            goto out_trigger;
        }

        aept_trigger_ctx_collect_dirs(ctx, &tctx, candidates[i]);
        r = aept_do_remove(ctx, candidates[i], NULL, NULL, NULL);
        if (r < 0) {
            had_error = 1;
            if (!ctx->config.force_depends && !ctx->config.keep_going)
                goto out_trigger;
        }
    }

    aept_trigger_run_all(ctx, &tctx);
    r = had_error ? -1 : 0;

out_trigger:
    aept_trigger_ctx_free(&tctx);

out_needed:
    free(candidates_evr);
    free(candidates);
    free(needed);
out_fileset:
    aept_fileset_free(&auto_set);
out:
    aept_solver_fini(ctx);
    return r;
}
