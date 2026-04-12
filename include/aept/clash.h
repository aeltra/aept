/* clash.h - file clash detection
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef CLASH_H_7BF97F
#define CLASH_H_7BF97F

#include <solv/pool.h>

#include "aept/owner_index.h"
#include "aept/util.h"

struct aept_ctx;

/* Check for file clashes before extracting a package.
 * old_files: fileset of old version (for upgrades), or NULL.
 * owners: file->owner index covering the current transaction state.
 * Returns the number of clashes (0 = OK), -1 on error. */
int aept_clash_check(struct aept_ctx *ctx, const char *ipk_path,
                     Pool *pool, Id p,
                     aept_fileset_t *old_files,
                     aept_owner_index_t *owners);

#endif
