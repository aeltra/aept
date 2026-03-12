/* update.h - fetch package lists from repositories
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef UPDATE_H_7BF97F
#define UPDATE_H_7BF97F

struct aept_ctx;

/* Fetch Packages lists and signatures for all configured sources. */
int aept_op_update(struct aept_ctx *ctx);

#endif
