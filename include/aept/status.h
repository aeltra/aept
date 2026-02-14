/* status.h - installed package database
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef STATUS_H_7BF97F
#define STATUS_H_7BF97F

/* Load the status file into the solver as the installed repo. */
int status_load(void);

/* Write the status file from the currently installed solvables. */
int status_write(void);

/* Append a package entry to the status file. */
int status_add(const char *control_path);

/* Remove a package entry from the status file by name. */
int status_remove(const char *name);

#endif
