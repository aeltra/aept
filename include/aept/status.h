/* status.h - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef STATUS_H_7BF97F
#define STATUS_H_7BF97F

#include "aept/util.h"

struct aept_ctx;

/* Load the installed-package database from the .control files in
 * {info_dir} into the solver as the installed repo. */
int aept_status_load(struct aept_ctx *ctx);

/* Read raw control fields from control_src, append a
 * "Status: install ok <state>" line, and write the result to
 * dest_path atomically (tmp + rename). */
int aept_status_add(struct aept_ctx *ctx, const char *control_src, const char *dest_path,
                    const char *state);

/*
 * The mark an installed package carries.  Manual is the default and is
 * recorded as nothing; the other two are lines in the marks file.
 */
typedef enum {
    AEPT_MARK_MANUAL = 0,
    AEPT_MARK_AUTO,
    AEPT_MARK_PROTECTED,
} aept_mark_t;

aept_mark_t aept_status_get_mark(struct aept_ctx *ctx, const char *name);

/* Set the mark, replacing whatever the package carried.  MANUAL drops
 * the line.  Setting on a name with no line to MANUAL is a no-op. */
int aept_status_set_mark(struct aept_ctx *ctx, const char *name, aept_mark_t mark);

/* Every package carrying the given mark, into a sorted fileset. */
int aept_status_load_marked(struct aept_ctx *ctx, aept_mark_t mark, aept_fileset_t *set);

/* Every auto mark becomes manual; protected ones stay. */
int aept_status_clear_auto(struct aept_ctx *ctx);

/* The state word of a package's Status line ("installed", "unpacked",
 * "triggers-pending") into buf; 0 when found, -1 otherwise. */
int aept_status_get_state(struct aept_ctx *ctx, const char *name, char *buf, size_t buflen);

/* Rewrite the Status line to the given state, everything else kept. */
int aept_status_set_state(struct aept_ctx *ctx, const char *name, const char *state);

#endif
