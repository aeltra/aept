/* archive.h - IPK archive extraction via libarchive
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef ARCHIVE_H_7BF97F
#define ARCHIVE_H_7BF97F

#include <stdio.h>

#include "aept/util.h"

struct aept_ar {
    struct archive *ar;
    int extract_flags;
};

/* Open the control tarball from an IPK file. */
struct aept_ar *aept_ar_open_pkg_control_archive(const char *filename);

/* Open the data tarball from an IPK file. */
struct aept_ar *aept_ar_open_pkg_data_archive(const char *filename);

/* Open a gzip-compressed file for streaming decompression. */
struct aept_ar *aept_ar_open_compressed_file(const char *filename);

/* Copy decompressed content to a stream. */
int aept_ar_copy_to_stream(struct aept_ar *ar, FILE *stream);

/* Extract a named file from the archive to a stream. */
int aept_ar_extract_file_to_stream(struct aept_ar *ar, const char *filename,
                              FILE *stream);

/* Write file paths from the archive to a stream. */
int aept_ar_extract_paths_to_stream(struct aept_ar *ar, FILE *stream);

/* Extract all files to a directory.
 * If conffiles is non-NULL, entries whose paths match the set are
 * extracted with cf_suffix appended to the destination pathname
 * (e.g. ".aept-new") instead of overwriting the original. */
int aept_ar_extract_all(struct aept_ar *ar, const char *prefix,
                   unsigned long *size, aept_fileset_t *conffiles,
                   const char *cf_suffix);

/* Extract only files whose paths are in the given set.
 * Clears NO_OVERWRITE so that existing files are replaced. */
int aept_ar_extract_selected(struct aept_ar *ar, aept_fileset_t *selected,
                        const char *prefix);

typedef struct {
    char *path;
    char *link_target;  /* NULL if not a symlink */
} aept_ar_file_entry_t;

typedef struct {
    aept_ar_file_entry_t *entries;
    int count;
    int alloc;
} aept_ar_file_list_t;

void aept_ar_file_list_init(aept_ar_file_list_t *fl);
void aept_ar_file_list_free(aept_ar_file_list_t *fl);

/* List non-directory file paths from an IPK's data archive.
 * Fills out with archive paths (e.g. "./usr/bin/foo") and symlink
 * targets where applicable.  Returns 0 on success, -1 on error. */
int aept_ar_list_data_paths(const char *ipk_path, aept_ar_file_list_t *out);

/* Close and free archive handle. */
void aept_ar_close(struct aept_ar *ar);

#endif
