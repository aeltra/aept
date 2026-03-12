/* autoremove.h - remove unneeded auto-installed packages
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef AUTOREMOVE_H_7BF97F
#define AUTOREMOVE_H_7BF97F

struct aept_ctx;

/* Remove auto-installed packages that are no longer needed. */
int aept_op_autoremove(struct aept_ctx *ctx);

#endif
