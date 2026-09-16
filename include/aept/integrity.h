/* integrity.h - what is on disk against what the packages recorded
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef INTEGRITY_H_7BF97F
#define INTEGRITY_H_7BF97F

#include "aept/aept.h"
#include "aept/internal.h"

/*
 * Check every file of the named packages -- every installed package
 * when count is 0 -- against its .list record and append a line to
 * `out` for each discrepancy.  Returns 0 when every package was
 * checked (differences or not), 1 when a named package is not
 * installed, -1 on an error reading the records.
 */
int aept_op_verify(struct aept_ctx *ctx, const char **names, int count, aept_verify_list_t *out);

#endif
