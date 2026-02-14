/* config.h - configuration file parsing
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AEPT_CONFIG_H_7BF97F
#define AEPT_CONFIG_H_7BF97F

int config_load(const char *filename);
void config_free(void);

/* Return path prefixed with offline_root if set. Caller must free. */
char *config_root_path(const char *path);

#endif
