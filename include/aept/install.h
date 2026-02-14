/* install.h - install/upgrade orchestration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef INSTALL_H_7BF97F
#define INSTALL_H_7BF97F

/* Install packages by name. If names is NULL, upgrade all.
 * Resolves dependencies via solver. */
int aept_install(const char **names, int count);

#endif
