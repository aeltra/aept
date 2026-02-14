/* remove.h - remove orchestration
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef REMOVE_H_7BF97F
#define REMOVE_H_7BF97F

#include "aept/util.h"

/* Remove packages by name. Resolves reverse dependencies via solver. */
int aept_remove(const char **names, int count);

/* Remove a single package (used internally by install for upgrades).
 * new_version: version of the replacing package (NULL for pure removal).
 * Controls maintainer script arguments: "upgrade <ver>" vs "remove".
 * protected: files installed earlier in the same transaction (skipped
 * during removal). May be NULL. */
int aept_do_remove(const char *name, const char *new_version,
                   const aept_fileset_t *protected);

#endif
