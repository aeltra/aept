/* status.h - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef STATUS_H_7BF97F
#define STATUS_H_7BF97F

#include "aept/util.h"

struct aept_ctx;

/* Load the installed-package database from {info_dir}/*.control into
 * the solver as the installed repo. */
int aept_status_load(struct aept_ctx *ctx);

/* Read raw control fields from control_src, append a
 * "Status: install ok <state>" line, and write the result to
 * dest_path atomically (tmp + rename). */
int aept_status_add(struct aept_ctx *ctx, const char *control_src,
                    const char *dest_path, const char *state);

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
