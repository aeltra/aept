/* query.h - read-only query commands
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QUERY_H_7BF97F
#define QUERY_H_7BF97F

/* Search which installed package owns a file path.
 * Prints the package name to stdout. Returns 0 if found, 1 if not. */
int aept_search(const char *path);

/* Print configured architectures (one per line).
 * Returns 0 on success. */
int aept_print_architecture(void);

#endif
