/* pin.h - version pinning
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PIN_H_7BF97F
#define PIN_H_7BF97F

/* Pin a package to a specific version.
 * Overwrites any existing pin for the same name. */
int pin_add(const char *name, const char *version);

/* Remove a pin for a package. Returns 0 even if no pin existed. */
int pin_remove(const char *name);

/* Look up the pinned version for a package.
 * Returns a newly allocated string, or NULL if not pinned. Caller must free. */
char *pin_lookup(const char *name);

/* Load all pins and register them with the solver.
 * Must be called after solver_init() + status_load() + load_repos(),
 * before solver_resolve_install(). */
int pin_load_into_solver(void);

#endif
