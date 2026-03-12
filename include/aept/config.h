/* config.h - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef AEPT_CONFIG_H_7BF97F
#define AEPT_CONFIG_H_7BF97F

struct aept_ctx;
struct aept_config;

void aept_config_set_defaults(struct aept_config *cfg);
int aept_config_load(struct aept_config *cfg, const char *filename);
void aept_config_free(struct aept_config *cfg);

/* Apply offline_root prefix to all configured paths. Must be called
 * after aept_config_load() and any command-line overrides of offline_root. */
void aept_config_apply_offline_root(struct aept_config *cfg);

/* Return path prefixed with offline_root if set. Caller must free. */
char *aept_config_root_path(const struct aept_config *cfg, const char *path);

/* Validate config values (paths exist and have expected types). */
int aept_config_validate(const struct aept_config *cfg);

/* Acquire/release exclusive lock to prevent concurrent aept instances. */
int aept_config_lock(struct aept_ctx *ctx);
void aept_config_unlock(struct aept_ctx *ctx);

#endif
