/* config.h - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AEPT_CONFIG_H_7BF97F
#define AEPT_CONFIG_H_7BF97F

int config_load(const char *filename);
void config_free(void);

/* Apply offline_root prefix to all configured paths. Must be called
 * after config_load() and any command-line overrides of offline_root. */
void config_apply_offline_root(void);

/* Return path prefixed with offline_root if set. Caller must free. */
char *config_root_path(const char *path);

/* Acquire/release exclusive lock to prevent concurrent aept instances. */
int config_lock(void);
void config_unlock(void);

#endif
