/* util.h - utility functions
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UTIL_H_7BF97F
#define UTIL_H_7BF97F

#include <stddef.h>

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
int xasprintf(char **strp, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

int file_exists(const char *path);
int file_is_dir(const char *path);
int file_copy(const char *src, const char *dst);
int file_mkdir_hier(const char *path, int mode);

int xsystem(const char *argv[]);
int xsystem_offline_root(const char *argv[]);

#endif
