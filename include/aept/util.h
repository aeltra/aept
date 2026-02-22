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

void *aept_malloc(size_t size);
void *aept_realloc(void *ptr, size_t size);
char *aept_strdup(const char *s);
int aept_asprintf(char **strp, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

int aept_pkg_name_is_safe(const char *name);
int aept_symlink_target_is_safe(const char *target);
int aept_archive_path_is_safe(const char *path);
int aept_fgets_is_truncated(const char *buf, size_t bufsize);
void aept_fgets_drain_line(FILE *fp);
int aept_file_exists(const char *path);
int aept_file_is_dir(const char *path);
int aept_file_copy(const char *src, const char *dst);
int aept_file_mkdir_hier(const char *path, mode_t mode);

void aept_signal_setup(void);
int aept_signal_was_interrupted(void);

int aept_system(const char *argv[]);
int aept_system_offline_root(const char *argv[]);

void aept_fileset_init(aept_fileset_t *fs);
void aept_fileset_add(aept_fileset_t *fs, const char *path);
void aept_fileset_sort(aept_fileset_t *fs);
int aept_fileset_contains(aept_fileset_t *fs, const char *path);
void aept_fileset_free(aept_fileset_t *fs);

#endif
