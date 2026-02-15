/* status.h - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef STATUS_H_7BF97F
#define STATUS_H_7BF97F

#include "aept/util.h"

/* Load the status file into the solver as the installed repo. */
int status_load(void);

/* Append a package entry to the status file.
 * state: "installed" or "unpacked" (postinst failed). */
int status_add(const char *control_path, const char *state);

/* Remove a package entry from the status file by name. */
int status_remove(const char *name);

/* Mark a package as auto-installed. */
int status_mark_auto(const char *name);

/* Unmark a package as auto-installed. */
int status_unmark_auto(const char *name);

/* Check whether a package is marked auto-installed. */
int status_is_auto(const char *name);

/* Clear all auto-installed marks. */
int status_clear_auto(void);

/* Load the set of auto-installed package names into a fileset. */
int status_load_auto_set(aept_fileset_t *set);

#endif
