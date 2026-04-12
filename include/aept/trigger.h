/* trigger.h - trigger processing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef TRIGGER_H_7BF97F
#define TRIGGER_H_7BF97F

struct aept_ctx;

/* Transaction-scoped trigger context.  Accumulates modified directories
 * during a transaction; fires trigger scripts after all steps complete. */
typedef struct {
    char **dirs;          /* unique directory paths (no leading /) */
    int n_dirs;
    int dirs_alloc;
    int dirs_sorted;

    char **fresh_pkgs;    /* packages freshly installed/upgraded */
    int n_fresh;
    int fresh_alloc;
} aept_trigger_ctx_t;

void aept_trigger_ctx_init(aept_trigger_ctx_t *tctx);
void aept_trigger_ctx_free(aept_trigger_ctx_t *tctx);

/* Record a directory path as modified.  Deduplicates. */
void aept_trigger_ctx_add_dir(aept_trigger_ctx_t *tctx, const char *dir);

/* Record a package as freshly installed/upgraded. */
void aept_trigger_ctx_add_fresh(aept_trigger_ctx_t *tctx, const char *name);

/* Collect parent directories of all files in a .list file into tctx. */
int aept_trigger_ctx_collect_dirs(struct aept_ctx *ctx,
                                  aept_trigger_ctx_t *tctx,
                                  const char *name);

/* Fire all pending triggers after transaction completes.
 * Scans info_dir/*.triggers directly to find interested packages. */
int aept_trigger_run_all(struct aept_ctx *ctx, aept_trigger_ctx_t *tctx);

#endif
