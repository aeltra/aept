/* archive.h - IPK archive extraction via libarchive
 *
 * Adapted from opkg (Copyright (C) 2014 Paul Barker, GPL-2.0-or-later).
 */

#ifndef ARCHIVE_H_7BF97F
#define ARCHIVE_H_7BF97F

#include <stdio.h>

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

/* Extract all files to a directory. */
int ar_extract_all(struct aept_ar *ar, const char *prefix,
                   unsigned long *size);

/* Close and free archive handle. */
void ar_close(struct aept_ar *ar);

#endif
