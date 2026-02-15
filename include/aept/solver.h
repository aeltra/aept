/* solver.h - libsolv integration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SOLVER_H_7BF97F
#define SOLVER_H_7BF97F

#include <stdio.h>

#include <solv/pool.h>
#include <solv/transaction.h>

/* Initialize the solver pool. */
int solver_init(void);

/* Load a Packages file into the pool as an available repo.
 * source_index is the index into cfg->sources for download URL mapping. */
int solver_load_repo(const char *name, FILE *fp, int source_index);

/* Load the installed package database into the pool. */
int solver_load_installed(FILE *fp);

/* Resolve: install named packages. If names is NULL, upgrade all. */
int solver_resolve_install(const char **names, int count);

/* Resolve: remove named packages. */
int solver_resolve_remove(const char **names, int count);

/* Access the computed transaction. NULL if no solve has been done. */
Transaction *solver_transaction(void);

/* Get the pool. */
Pool *solver_pool(void);

/* Get the source index for a solvable (which repo/source it came from). */
int solver_solvable_source_index(Id p);

/* Find the best available (non-installed) solvable for a package name.
 * Returns the solvable Id, or 0 if not found. */
Id solver_find_available(const char *name);

/* Register a version pin. The solver will lock this package during
 * upgrade-all, and install the exact pinned version during install. */
void solver_add_pin(const char *name, const char *version);

/* Clear all registered pins (called by solver_fini). */
void solver_clear_pins(void);

/* Free all solver state. */
void solver_fini(void);

#endif
