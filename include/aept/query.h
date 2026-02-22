/* query.h - read-only query commands
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QUERY_H_7BF97F
#define QUERY_H_7BF97F

/* Find which installed package owns a file path.
 * Prints the package name to stdout. Returns 0 if found, 1 if not. */
int aept_op_owns(const char *path);

/* Print configured architectures (one per line).
 * Returns 0 on success. */
int aept_op_print_architecture(void);

/* List packages. If pattern is non-NULL, filter by glob on name.
 * filter_installed: only installed. filter_upgradable: only upgradable. */
int aept_op_list(const char *pattern, int filter_installed, int filter_upgradable);

/* List files belonging to an installed package.
 * Prints file paths to stdout. Returns 0 if found, 1 if not installed. */
int aept_op_files(const char *name);

/* Show package metadata from available repos (with installed fallback).
 * Prints metadata to stdout. Returns 0 if found, 1 if not. */
int aept_op_show(const char *name);

#endif
