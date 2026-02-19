/* archive.h - IPK archive extraction via libarchive
 *
 * Copyright (C) 2014 Paul Barker (original opkg implementation)
 * Copyright (C) 2026 Tobias Koch (adapted for aept)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ARCHIVE_H_7BF97F
#define ARCHIVE_H_7BF97F

#include <stdio.h>

#include "aept/util.h"

struct aept_ar {
    struct archive *ar;
    int extract_flags;
};

/* Open control.tar.gz from an IPK file. */
struct aept_ar *ar_open_pkg_control_archive(const char *filename);

/* Open data.tar.gz from an IPK file. */
struct aept_ar *ar_open_pkg_data_archive(const char *filename);

/* Open a gzip-compressed file for streaming decompression. */
struct aept_ar *ar_open_compressed_file(const char *filename);

/* Copy decompressed content to a stream. */
int ar_copy_to_stream(struct aept_ar *ar, FILE *stream);

/* Extract a named file from the archive to a stream. */
int ar_extract_file_to_stream(struct aept_ar *ar, const char *filename,
                              FILE *stream);

/* Write file paths from the archive to a stream. */
int ar_extract_paths_to_stream(struct aept_ar *ar, FILE *stream);

/* Extract all files to a directory.
 * If conffiles is non-NULL, entries whose paths match the set are
 * extracted with cf_suffix appended to the destination pathname
 * (e.g. ".aept-new") instead of overwriting the original. */
int ar_extract_all(struct aept_ar *ar, const char *prefix,
                   unsigned long *size, aept_fileset_t *conffiles,
                   const char *cf_suffix);

/* Extract only files whose paths are in the given set.
 * Clears NO_OVERWRITE so that existing files are replaced. */
int ar_extract_selected(struct aept_ar *ar, aept_fileset_t *selected,
                        const char *prefix);

typedef struct {
    char *path;
    char *link_target;  /* NULL if not a symlink */
} ar_file_entry_t;

typedef struct {
    ar_file_entry_t *entries;
    int count;
    int alloc;
} ar_file_list_t;

void ar_file_list_init(ar_file_list_t *fl);
void ar_file_list_free(ar_file_list_t *fl);

/* List non-directory file paths from an IPK's data archive.
 * Fills out with archive paths (e.g. "./usr/bin/foo") and symlink
 * targets where applicable.  Returns 0 on success, -1 on error. */
int ar_list_data_paths(const char *ipk_path, ar_file_list_t *out);

/* Close and free archive handle. */
void ar_close(struct aept_ar *ar);

#endif
