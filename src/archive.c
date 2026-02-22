/* archive.c - IPK archive extraction via libarchive
 *
 * Copyright (C) 2014 Paul Barker (original opkg implementation)
 * Copyright (C) 2026 Tobias Koch (adapted for aept)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "aept/aept.h"
#include "aept/archive.h"
#include "aept/msg.h"
#include "aept/util.h"

#define EXTRACT_BUFFER_LEN 0x8000

/*
 * Internal functions
 */

struct inner_data {
    struct archive *outer;
    void *buffer;
};

static ssize_t inner_read(struct archive *a, void *client_data,
                          const void **buff)
{
    (void)a;

    struct inner_data *data = (struct inner_data *)client_data;

    *buff = data->buffer;
    return archive_read_data(data->outer, data->buffer, EXTRACT_BUFFER_LEN);
}

static int inner_close(struct archive *inner, void *client_data)
{
    (void)inner;

    struct inner_data *data = (struct inner_data *)client_data;

    archive_read_free(data->outer);
    free(data->buffer);
    free(data);

    return ARCHIVE_OK;
}

static size_t read_data(struct archive *a, void *buffer, size_t len, int *eof)
{
    ssize_t r;
    int retries = 0;

    if (eof)
        *eof = 0;

retry:
    r = archive_read_data(a, buffer, len);
    if (r > 0) {
        return (size_t)r;
    }
    switch (r) {
    case 0:
        if (eof)
            *eof = 1;
        return 0;

    case ARCHIVE_WARN:
        aept_log_error("warning when reading archive data: %s",
                 archive_error_string(a));
        return 0;

    case ARCHIVE_RETRY:
        aept_log_error("failed to read archive data: %s",
                 archive_error_string(a));
        if (retries++ < 3) {
            aept_log_warning("retrying...");
            goto retry;
        }
        return 0;

    default:
        aept_log_error("failed to read archive data: %s",
                 archive_error_string(a));
        return 0;
    }
}

static int copy_to_stream(struct archive *a, FILE *stream)
{
    void *buffer;
    size_t sz_out, sz_in;
    int eof;
    size_t len = EXTRACT_BUFFER_LEN;

    if (archive_format(a) == ARCHIVE_FORMAT_EMPTY)
        return 0;

    buffer = aept_malloc(len);

    while (1) {
        sz_in = read_data(a, buffer, len, &eof);
        if (eof) {
            free(buffer);
            return 0;
        }
        if (sz_in == 0)
            goto err_cleanup;

        sz_out = fwrite(buffer, 1, sz_in, stream);
        if (sz_out < sz_in) {
            aept_log_error("failed to write data to stream: %s",
                     strerror(errno));
            goto err_cleanup;
        }
    }

err_cleanup:
    free(buffer);
    return -1;
}

/*
 * Lexically normalize a path by resolving '.' and '..' components.
 * Does not touch the filesystem. Caller must free the result.
 */
static char *path_normalize(const char *path)
{
    int is_abs = (path[0] == '/');
    char *dup = aept_strdup(path);
    char *saveptr = NULL;
    char *tok;

    /* Collect resolved components on a stack */
    size_t cap = 16;
    size_t n = 0;
    char **parts = aept_malloc(cap * sizeof(char *));

    for (tok = strtok_r(dup, "/", &saveptr); tok;
         tok = strtok_r(NULL, "/", &saveptr)) {
        if (strcmp(tok, ".") == 0)
            continue;
        if (strcmp(tok, "..") == 0) {
            if (n > 0)
                n--;
            continue;
        }
        if (n == cap) {
            cap *= 2;
            parts = aept_realloc(parts, cap * sizeof(char *));
        }
        parts[n++] = tok;
    }

    /* Calculate result length */
    size_t len = is_abs ? 1 : 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0)
            len++;
        len += strlen(parts[i]);
    }

    char *result = aept_malloc(len + 1);
    char *p = result;

    if (is_abs)
        *p++ = '/';
    for (size_t i = 0; i < n; i++) {
        if (i > 0)
            *p++ = '/';
        size_t clen = strlen(parts[i]);
        memcpy(p, parts[i], clen);
        p += clen;
    }
    *p = '\0';

    free(parts);
    free(dup);
    return result;
}

static char *join_paths(const char *left, const char *right)
{
    char *path;

    while (right[0] == '.' && right[1] == '/')
        right += 2;
    while (right[0] == '/')
        right++;

    if (right[0] == '.' && right[1] == '\0')
        return NULL;

    if (right[0] == '\0')
        return NULL;

    if (!left)
        return aept_strdup(right);

    size_t len = strlen(left);
    while (len > 1 && left[len - 1] == '/')
        len--;

    aept_asprintf(&path, "%.*s/%s", (int)len, left, right);

    char *normalized = path_normalize(path);
    free(path);

    char *norm_prefix = path_normalize(left);
    size_t prefix_len = strlen(norm_prefix);

    if (strncmp(normalized, norm_prefix, prefix_len) != 0 ||
            (normalized[prefix_len] != '/' &&
             normalized[prefix_len] != '\0')) {
        aept_log_error("path '%s' escapes extraction directory", right);
        free(normalized);
        free(norm_prefix);
        return NULL;
    }

    free(norm_prefix);
    return normalized;
}

static int transform_dest_path(struct archive_entry *entry, const char *dest)
{
    char *path;
    const char *filename;

    filename = archive_entry_pathname(entry);

    path = join_paths(dest, filename);
    if (!path)
        return 1;

    archive_entry_set_pathname(entry, path);
    free(path);

    return 0;
}

static int transform_all_paths(struct archive_entry *entry, const char *dest)
{
    char *path;
    const char *filename;
    int r;

    r = transform_dest_path(entry, dest);
    if (r)
        return r;

    filename = archive_entry_hardlink(entry);
    if (filename) {
        path = join_paths(dest, filename);
        if (!path) {
            aept_log_error("not extracting '%s': hardlink to nowhere",
                     archive_entry_pathname(entry));
            return 1;
        }

        archive_entry_set_hardlink(entry, path);
        free(path);
    }

    return 0;
}

static struct archive_entry *read_header(struct archive *ar, int *eof)
{
    struct archive_entry *entry;
    int r;
    int retries = 0;

    if (eof)
        *eof = 0;

retry:
    r = archive_read_next_header(ar, &entry);
    switch (r) {
    case ARCHIVE_OK:
        break;

    case ARCHIVE_WARN:
        aept_log_debug("warning when reading archive header: %s",
                 archive_error_string(ar));
        break;

    case ARCHIVE_EOF:
        if (eof)
            *eof = 1;
        return NULL;

    case ARCHIVE_RETRY:
        aept_log_error("failed to read archive header: %s",
                 archive_error_string(ar));
        if (retries++ < 3)
            goto retry;
        return NULL;

    default:
        aept_log_error("failed to read archive header: %s",
                 archive_error_string(ar));
        return NULL;
    }

    return entry;
}

static int extract_file_to_stream(struct archive *a, const char *name,
                                  FILE *stream)
{
    struct archive_entry *entry;
    const char *path;

    while (1) {
        entry = read_header(a, NULL);
        if (!entry)
            return -1;

        transform_dest_path(entry, NULL);

        path = archive_entry_pathname(entry);
        if (strcmp(path, name) == 0)
            return copy_to_stream(a, stream);
    }
}

static int extract_paths_to_stream(struct archive *a, FILE *stream)
{
    struct archive_entry *entry;
    int r;
    const char *path;
    const struct stat *entry_stat;
    int eof;

    while (1) {
        entry = read_header(a, &eof);
        if (eof)
            return 0;
        if (!entry)
            return -1;

        path = archive_entry_pathname(entry);

        if (!aept_archive_path_is_safe(path)) {
            aept_log_error("refusing unsafe archive path '%s'", path);
            return -1;
        }

        entry_stat = archive_entry_stat(entry);
        if (S_ISLNK(entry_stat->st_mode)) {
            const char *target = archive_entry_symlink(entry);
            if (!aept_symlink_target_is_safe(target))
                target = "<redacted>";
            r = fprintf(stream, "%s\t%#03o\t%s\n", path,
                        (unsigned int)entry_stat->st_mode, target);
        } else {
            r = fprintf(stream, "%s\t%#03o\n", path,
                        (unsigned int)entry_stat->st_mode);
        }
        if (r <= 0) {
            aept_log_error("failed to write path to stream: %s",
                     strerror(errno));
            return -1;
        }
    }
}

static struct archive *open_disk(int flags)
{
    struct archive *disk;
    int r;

    disk = archive_write_disk_new();
    if (!disk) {
        aept_log_error("failed to create disk archive object");
        return NULL;
    }

    r = archive_write_disk_set_options(disk, flags);
    if (r == ARCHIVE_WARN)
        aept_log_debug("warning when setting disk options: %s",
                 archive_error_string(disk));
    else if (r != ARCHIVE_OK) {
        aept_log_error("failed to set disk options: %s",
                 archive_error_string(disk));
        goto err_cleanup;
    }

    r = archive_write_disk_set_standard_lookup(disk);
    if (r == ARCHIVE_WARN)
        aept_log_debug(
                 "warning when setting user/group lookup functions: %s",
                 archive_error_string(disk));
    else if (r != ARCHIVE_OK) {
        aept_log_error("failed to set user/group lookup functions: %s",
                 archive_error_string(disk));
        goto err_cleanup;
    }

    return disk;

err_cleanup:
    archive_write_free(disk);
    return NULL;
}

static int extract_all(struct archive *a, const char *dest, int flags,
                       unsigned long *size, aept_fileset_t *conffiles,
                       const char *cf_suffix)
{
    struct archive *disk;
    struct archive *cf_disk = NULL;
    struct archive_entry *entry;
    int r;
    int eof;
    int cf_flags;

    disk = open_disk(flags);
    if (!disk)
        return -1;

    if (conffiles && conffiles->count > 0) {
        cf_flags = flags & ~ARCHIVE_EXTRACT_NO_OVERWRITE;
        cf_disk = open_disk(cf_flags);
        if (!cf_disk) {
            archive_write_free(disk);
            return -1;
        }
    }

    while (1) {
        int is_conffile = 0;

        entry = read_header(a, &eof);
        if (eof)
            break;
        if (!entry) {
            r = -1;
            goto err_cleanup;
        }

        if (conffiles && conffiles->count > 0)
            is_conffile = aept_fileset_contains(conffiles,
                                           archive_entry_pathname(entry));

        r = transform_all_paths(entry, dest);
        if (r == 1)
            continue;
        if (r < 0) {
            aept_log_error("failed to transform path");
            goto err_cleanup;
        }

        if (is_conffile && cf_suffix) {
            const char *pathname = archive_entry_pathname(entry);
            char *suffixed = NULL;
            aept_asprintf(&suffixed, "%s%s", pathname, cf_suffix);
            archive_entry_set_pathname(entry, suffixed);
            free(suffixed);
        }

        aept_log_debug("extracting '%s'",
                 archive_entry_pathname(entry));

        r = archive_read_extract2(a, entry,
                                  is_conffile ? cf_disk : disk);
        switch (r) {
        case ARCHIVE_OK:
            break;
        case ARCHIVE_WARN:
            aept_log_debug("warning extracting '%s': %s",
                     archive_entry_pathname(entry),
                     archive_error_string(a));
            break;
        default:
            aept_log_error("failed to extract '%s': %s",
                     archive_entry_pathname(entry),
                     archive_error_string(a));
            r = -1;
            goto err_cleanup;
        }

        if (size)
            *size += archive_entry_size(entry);
    }

    r = ARCHIVE_OK;
err_cleanup:
    if (cf_disk)
        archive_write_free(cf_disk);
    archive_write_free(disk);
    return (r == ARCHIVE_OK) ? 0 : -1;
}

/*
 * Open outer AR archive
 */

static struct archive *open_outer(const char *filename)
{
    struct archive *outer;
    int r;

    outer = archive_read_new();
    if (!outer) {
        aept_log_error("failed to create outer archive object");
        return NULL;
    }

    r = archive_read_support_format_ar(outer);
    if (r != ARCHIVE_OK) {
        aept_log_error("ar format not supported: %s",
                 archive_error_string(outer));
        goto err_cleanup;
    }
    r = archive_read_support_filter_gzip(outer);
    if (r == ARCHIVE_WARN)
        aept_log_info("gzip support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("gzip format not supported");
        goto err_cleanup;
    }
    r = archive_read_support_format_tar(outer);
    if (r != ARCHIVE_OK) {
        aept_log_error("tar format not supported: %s",
                 archive_error_string(outer));
        goto err_cleanup;
    }

    r = archive_read_open_filename(outer, filename, EXTRACT_BUFFER_LEN);
    if (r != ARCHIVE_OK) {
        aept_log_error("failed to open package '%s': %s",
                 filename, archive_error_string(outer));
        goto err_cleanup;
    }
    return outer;

err_cleanup:
    archive_read_free(outer);
    return NULL;
}

/*
 * Open inner compressed tar archive
 */

static struct archive *open_inner(struct archive *outer)
{
    struct archive *inner;
    struct inner_data *data;
    int r;

    inner = archive_read_new();
    if (!inner) {
        aept_log_error("failed to create inner archive object");
        return NULL;
    }

    data = (struct inner_data *)aept_malloc(sizeof(struct inner_data));
    data->buffer = aept_malloc(EXTRACT_BUFFER_LEN);
    data->outer = outer;

    r = archive_read_support_filter_gzip(inner);
    if (r == ARCHIVE_WARN)
        aept_log_info("gzip support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("gzip format not supported");
        goto err_cleanup;
    }

#if HAVE_XZ
    r = archive_read_support_filter_xz(inner);
    if (r == ARCHIVE_WARN)
        aept_log_info("xz support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("xz format not supported");
        goto err_cleanup;
    }
#endif

#if HAVE_BZIP2
    r = archive_read_support_filter_bzip2(inner);
    if (r == ARCHIVE_WARN)
        aept_log_info("bzip2 support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("bzip2 format not supported");
        goto err_cleanup;
    }
#endif

#if HAVE_LZ4
    r = archive_read_support_filter_lz4(inner);
    if (r == ARCHIVE_WARN)
        aept_log_info("lz4 support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("lz4 format not supported");
        goto err_cleanup;
    }
#endif

#if HAVE_ZSTD
    r = archive_read_support_filter_zstd(inner);
    if (r == ARCHIVE_WARN)
        aept_log_info("zstd support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("zstd format not supported");
        goto err_cleanup;
    }
#endif

    r = archive_read_support_format_tar(inner);
    if (r != ARCHIVE_OK) {
        aept_log_error("tar format not supported: %s",
                 archive_error_string(inner));
        goto err_cleanup;
    }

    r = archive_read_support_format_empty(inner);
    if (r != ARCHIVE_OK) {
        aept_log_error("empty format not supported: %s",
                 archive_error_string(inner));
        goto err_cleanup;
    }

    r = archive_read_open(inner, data, NULL, inner_read, inner_close);
    if (r != ARCHIVE_OK) {
        aept_log_error("failed to open inner archive: %s",
                 archive_error_string(inner));
        goto err_cleanup;
    }

    return inner;

err_cleanup:
    archive_read_free(inner);
    free(data->buffer);
    free(data);
    return NULL;
}

static int find_inner(struct archive *outer, const char *arname)
{
    const char *path;
    struct archive_entry *entry;

    while (1) {
        entry = read_header(outer, NULL);
        if (!entry)
            return -1;

        transform_dest_path(entry, NULL);

        path = archive_entry_pathname(entry);
        if (strcmp(path, arname) == 0)
            return 0;
    }
}

static struct archive *extract_outer(const char *filename, const char *arname)
{
    int r;
    struct archive *inner;
    struct archive *outer;

    outer = open_outer(filename);
    if (!outer)
        return NULL;

    r = find_inner(outer, arname);
    if (r < 0)
        goto err_cleanup;

    inner = open_inner(outer);
    if (!inner)
        goto err_cleanup;

    return inner;

err_cleanup:
    archive_read_free(outer);
    return NULL;
}

static struct archive *open_compressed(const char *filename)
{
    struct archive *ar;
    int r;

    ar = archive_read_new();
    if (!ar) {
        aept_log_error(
                 "failed to create archive object for compressed file");
        return NULL;
    }

    r = archive_read_support_filter_gzip(ar);
    if (r == ARCHIVE_WARN)
        aept_log_info("gzip support provided by external program");
    else if (r != ARCHIVE_OK) {
        aept_log_error("gzip format not supported: %s",
                 archive_error_string(ar));
        goto err_cleanup;
    }

    r = archive_read_support_format_raw(ar);
    if (r != ARCHIVE_OK) {
        aept_log_error("raw format not supported: %s",
                 archive_error_string(ar));
        goto err_cleanup;
    }

    r = archive_read_support_format_empty(ar);
    if (r != ARCHIVE_OK) {
        aept_log_error("empty format not supported: %s",
                 archive_error_string(ar));
        goto err_cleanup;
    }

    r = archive_read_open_filename(ar, filename, EXTRACT_BUFFER_LEN);
    if (r != ARCHIVE_OK) {
        aept_log_error("failed to open compressed file '%s': %s",
                 filename, archive_error_string(ar));
        goto err_cleanup;
    }

    return ar;

err_cleanup:
    archive_read_free(ar);
    return NULL;
}

/*
 * Public glue layer
 */

struct aept_ar *aept_ar_open_pkg_control_archive(const char *filename)
{
    struct aept_ar *ar;

    ar = (struct aept_ar *)aept_malloc(sizeof(struct aept_ar));

#if HAVE_ZSTD
    ar->ar = extract_outer(filename, "control.tar.zst");
#endif
    if (!ar->ar)
        ar->ar = extract_outer(filename, "control.tar.gz");
#if HAVE_XZ
    if (!ar->ar)
        ar->ar = extract_outer(filename, "control.tar.xz");
#endif
#if HAVE_BZIP2
    if (!ar->ar)
        ar->ar = extract_outer(filename, "control.tar.bz2");
#endif
#if HAVE_LZ4
    if (!ar->ar)
        ar->ar = extract_outer(filename, "control.tar.lz4");
#endif
    if (!ar->ar) {
        free(ar);
        return NULL;
    }

    ar->extract_flags = ARCHIVE_EXTRACT_SECURE_SYMLINKS |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT;

    return ar;
}

struct aept_ar *aept_ar_open_pkg_data_archive(const char *filename)
{
    struct aept_ar *ar;

    ar = (struct aept_ar *)aept_malloc(sizeof(struct aept_ar));

#if HAVE_ZSTD
    ar->ar = extract_outer(filename, "data.tar.zst");
#endif
    if (!ar->ar)
        ar->ar = extract_outer(filename, "data.tar.gz");
#if HAVE_XZ
    if (!ar->ar)
        ar->ar = extract_outer(filename, "data.tar.xz");
#endif
#if HAVE_BZIP2
    if (!ar->ar)
        ar->ar = extract_outer(filename, "data.tar.bz2");
#endif
#if HAVE_LZ4
    if (!ar->ar)
        ar->ar = extract_outer(filename, "data.tar.lz4");
#endif
    if (!ar->ar) {
        free(ar);
        return NULL;
    }

    ar->extract_flags = ARCHIVE_EXTRACT_OWNER | ARCHIVE_EXTRACT_PERM |
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_UNLINK |
        ARCHIVE_EXTRACT_NO_OVERWRITE | ARCHIVE_EXTRACT_SECURE_SYMLINKS |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT;

    if (aept_cfg->ignore_uid)
        ar->extract_flags &= ~ARCHIVE_EXTRACT_OWNER;

    return ar;
}

struct aept_ar *aept_ar_open_compressed_file(const char *filename)
{
    struct aept_ar *ar;
    struct archive_entry *entry;
    int eof;

    ar = (struct aept_ar *)aept_malloc(sizeof(struct aept_ar));

    ar->ar = open_compressed(filename);
    if (!ar->ar)
        goto err_cleanup;

    ar->extract_flags = 0;

    entry = read_header(ar->ar, &eof);
    if (!entry && !eof)
        goto err_cleanup;

    return ar;

err_cleanup:
    if (ar->ar)
        archive_read_free(ar->ar);
    free(ar);
    return NULL;
}

int aept_ar_copy_to_stream(struct aept_ar *ar, FILE *stream)
{
    return copy_to_stream(ar->ar, stream);
}

int aept_ar_extract_file_to_stream(struct aept_ar *ar, const char *filename,
                              FILE *stream)
{
    return extract_file_to_stream(ar->ar, filename, stream);
}

int aept_ar_extract_paths_to_stream(struct aept_ar *ar, FILE *stream)
{
    return extract_paths_to_stream(ar->ar, stream);
}

void aept_ar_file_list_init(aept_ar_file_list_t *fl)
{
    fl->entries = NULL;
    fl->count = 0;
    fl->alloc = 0;
}

void aept_ar_file_list_free(aept_ar_file_list_t *fl)
{
    for (int i = 0; i < fl->count; i++) {
        free(fl->entries[i].path);
        free(fl->entries[i].link_target);
    }
    free(fl->entries);
    aept_ar_file_list_init(fl);
}

static void file_list_add(aept_ar_file_list_t *fl, const char *path,
                           const char *link_target)
{
    if (fl->count >= fl->alloc) {
        fl->alloc = fl->alloc ? fl->alloc * 2 : 64;
        fl->entries = aept_realloc(fl->entries,
                               fl->alloc * sizeof(aept_ar_file_entry_t));
    }

    fl->entries[fl->count].path = aept_strdup(path);
    fl->entries[fl->count].link_target =
        link_target ? aept_strdup(link_target) : NULL;
    fl->count++;
}

int aept_ar_list_data_paths(const char *ipk_path, aept_ar_file_list_t *out)
{
    struct aept_ar *ar;
    struct archive_entry *entry;
    int eof;

    ar = aept_ar_open_pkg_data_archive(ipk_path);
    if (!ar)
        return -1;

    while (1) {
        entry = read_header(ar->ar, &eof);
        if (eof)
            break;
        if (!entry) {
            aept_ar_close(ar);
            return -1;
        }

        const char *path = archive_entry_pathname(entry);
        const struct stat *st = archive_entry_stat(entry);

        if (S_ISDIR(st->st_mode))
            continue;

        if (!aept_archive_path_is_safe(path)) {
            aept_log_error("refusing unsafe archive path '%s'", path);
            aept_ar_close(ar);
            return -1;
        }

        const char *target = NULL;
        if (S_ISLNK(st->st_mode))
            target = archive_entry_symlink(entry);

        file_list_add(out, path, target);
    }

    aept_ar_close(ar);
    return 0;
}

int aept_ar_extract_all(struct aept_ar *ar, const char *prefix, unsigned long *size,
                   aept_fileset_t *conffiles, const char *cf_suffix)
{
    return extract_all(ar->ar, prefix, ar->extract_flags, size,
                       conffiles, cf_suffix);
}

int aept_ar_extract_selected(struct aept_ar *ar, aept_fileset_t *selected,
                        const char *prefix)
{
    struct archive *disk;
    struct archive_entry *entry;
    int r;
    int eof;
    int flags;

    flags = ar->extract_flags & ~ARCHIVE_EXTRACT_NO_OVERWRITE;

    disk = open_disk(flags);
    if (!disk)
        return -1;

    while (1) {
        entry = read_header(ar->ar, &eof);
        if (eof)
            break;
        if (!entry) {
            r = -1;
            goto err_cleanup;
        }

        if (!aept_fileset_contains(selected, archive_entry_pathname(entry)))
            continue;

        r = transform_all_paths(entry, prefix);
        if (r == 1)
            continue;
        if (r < 0) {
            aept_log_error("failed to transform path");
            goto err_cleanup;
        }

        aept_log_debug("extracting conffile '%s'",
                  archive_entry_pathname(entry));

        r = archive_read_extract2(ar->ar, entry, disk);
        switch (r) {
        case ARCHIVE_OK:
            break;
        case ARCHIVE_WARN:
            aept_log_debug("warning extracting '%s': %s",
                      archive_entry_pathname(entry),
                      archive_error_string(ar->ar));
            break;
        default:
            aept_log_error("failed to extract '%s': %s",
                      archive_entry_pathname(entry),
                      archive_error_string(ar->ar));
            r = -1;
            goto err_cleanup;
        }
    }

    r = ARCHIVE_OK;
err_cleanup:
    archive_write_free(disk);
    return (r == ARCHIVE_OK) ? 0 : -1;
}

void aept_ar_close(struct aept_ar *ar)
{
    archive_read_free(ar->ar);
    free(ar);
}
