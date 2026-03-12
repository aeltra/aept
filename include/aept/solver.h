/* solver.h - libsolv integration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef SOLVER_H_7BF97F
#define SOLVER_H_7BF97F

#include <stdio.h>

#include <solv/pool.h>
#include <solv/solver.h>
#include <solv/transaction.h>

struct aept_ctx;

#define AEPT_MAX_REPOS   64
#define AEPT_MAX_CMDLINE 256

typedef struct {
    Id id;
    char *path;
} aept_cmdline_entry_t;

typedef struct {
    char *name;
    char *version;
} aept_pin_entry_t;

typedef struct aept_solver {
    Pool *pool;
    Repo *installed_repo;
    Repo *repos[AEPT_MAX_REPOS];
    int repo_source_index[AEPT_MAX_REPOS];
    int nrepos;
    Solver *solv;
    Transaction *trans;
    Repo *commandline_repo;
    aept_cmdline_entry_t cmdline_entries[AEPT_MAX_CMDLINE];
    int ncmdline;
    aept_pin_entry_t *pins;
    int npins;
} aept_solver_t;

/* --- Functions that need the full context (config + logging) ------------- */

int  aept_solver_init(struct aept_ctx *ctx);
void aept_solver_fini(struct aept_ctx *ctx);
int  aept_solver_load_repo(struct aept_ctx *ctx, const char *name,
                           FILE *fp, int source_index);
int  aept_solver_load_installed(struct aept_ctx *ctx, FILE *fp);
Id   aept_solver_load_local(struct aept_ctx *ctx, const char *path);
int  aept_solver_resolve_install(struct aept_ctx *ctx,
                                 const char **names, int count,
                                 const Id *local_ids, int local_count);
int  aept_solver_resolve_remove(struct aept_ctx *ctx,
                                const char **names, int count);

/* --- Pure solver accessors (no config/logging needed) ------------------- */

int aept_solver_is_commandline(aept_solver_t *s, Id p);
const char *aept_solver_commandline_path(aept_solver_t *s, Id p);
Transaction *aept_solver_transaction(aept_solver_t *s);
Pool *aept_solver_pool(aept_solver_t *s);
int aept_solver_solvable_source_index(aept_solver_t *s, Id p);
Id aept_solver_find_available(aept_solver_t *s, const char *name);
void aept_solver_add_pin(aept_solver_t *s, const char *name,
                         const char *version);
void aept_solver_clear_pins(aept_solver_t *s);
const char *aept_solver_installed_version(aept_solver_t *s, const char *name);

#endif
