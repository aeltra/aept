/* util.h - utility functions
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UTIL_H_7BF97F
#define UTIL_H_7BF97F

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    char **paths;
    int count;
    int alloc;
    int sorted;
} aept_fileset_t;

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
int xasprintf(char **strp, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

int pkg_name_is_safe(const char *name);
int file_exists(const char *path);
int file_is_dir(const char *path);
int file_copy(const char *src, const char *dst);
int file_mkdir_hier(const char *path, mode_t mode);

int xsystem(const char *argv[]);
int xsystem_offline_root(const char *argv[]);

void fileset_init(aept_fileset_t *fs);
void fileset_add(aept_fileset_t *fs, const char *path);
void fileset_sort(aept_fileset_t *fs);
int fileset_contains(const aept_fileset_t *fs, const char *path);
void fileset_free(aept_fileset_t *fs);

#endif
