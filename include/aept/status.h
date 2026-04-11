/* status.h - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef STATUS_H_7BF97F
#define STATUS_H_7BF97F

#include "aept/util.h"

struct aept_ctx;

/* Load the status file into the in-memory cache and into the solver
 * as the installed repo.  Any prior cache state is discarded. */
int aept_status_load(struct aept_ctx *ctx);

/* Append a package entry to the in-memory status cache.
 * state: "installed" or "unpacked" (postinst failed).  Not flushed to
 * disk until aept_status_flush is called. */
int aept_status_add(struct aept_ctx *ctx, const char *control_path,
                    const char *state);

/* Remove a package entry from the in-memory status cache by name.
 * Not flushed to disk until aept_status_flush is called. */
int aept_status_remove(struct aept_ctx *ctx, const char *name);

/* Write the cached status back to the status file if it has been
 * modified.  No-op if the cache is clean.  Returns 0 on success. */
int aept_status_flush(struct aept_ctx *ctx);

/* Free the in-memory status cache.  Called by aept_cleanup. */
void aept_status_cache_free(struct aept_ctx *ctx);

/* Mark a package as auto-installed. */
int aept_status_mark_auto(struct aept_ctx *ctx, const char *name);

/* Unmark a package as auto-installed. */
int aept_status_unmark_auto(struct aept_ctx *ctx, const char *name);

/* Check whether a package is marked auto-installed. */
int aept_status_is_auto(struct aept_ctx *ctx, const char *name);

/* Clear all auto-installed marks. */
int aept_status_clear_auto(struct aept_ctx *ctx);

/* Load the set of auto-installed package names into a fileset. */
int aept_status_load_auto_set(struct aept_ctx *ctx, aept_fileset_t *set);

#endif
