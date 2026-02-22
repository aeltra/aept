/* config.h - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AEPT_CONFIG_H_7BF97F
#define AEPT_CONFIG_H_7BF97F

void aept_config_set_defaults(void);
int aept_config_load(const char *filename);
void aept_config_free(void);

/* Apply offline_root prefix to all configured paths. Must be called
 * after aept_config_load() and any command-line overrides of offline_root. */
void aept_config_apply_offline_root(void);

/* Return path prefixed with offline_root if set. Caller must free. */
char *aept_config_root_path(const char *path);

/* Validate config values (paths exist and have expected types). */
int aept_config_validate(void);

/* Acquire/release exclusive lock to prevent concurrent aept instances. */
int aept_config_lock(void);
void aept_config_unlock(void);

#endif
