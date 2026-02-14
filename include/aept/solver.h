/* solver.h - libsolv integration */

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

/* Free all solver state. */
void solver_fini(void);

#endif
