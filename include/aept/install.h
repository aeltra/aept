/* install.h - install/upgrade orchestration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef INSTALL_H_7BF97F
#define INSTALL_H_7BF97F

struct aept_ctx;

/* Install packages by name and/or from local .aep files.
 * If all params are NULL/0, upgrade all.
 * Resolves dependencies via solver. */
int aept_op_install(struct aept_ctx *ctx, const char **names, int name_count,
                    const char **local_paths, int local_count);

#endif
