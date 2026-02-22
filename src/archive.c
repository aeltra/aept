/* archive.c - IPK archive extraction via libarchive
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/msg.h"
#include "aept/util.h"

#define BLOCK_SIZE 0x8000

/*
 * Pipe callbacks: feed data from one libarchive reader (the outer AR member)
 * into a second reader (the inner compressed tar).
 */

struct pipe_ctx {
    struct archive *source;
    char buf[BLOCK_SIZE];
};

static ssize_t pipe_read_cb(struct archive *a, void *opaque,
                            const void **out)
{
    (void)a;
    struct pipe_ctx *ctx = opaque;
    *out = ctx->buf;
    return archive_read_data(ctx->source, ctx->buf, sizeof(ctx->buf));
}

static int pipe_close_cb(struct archive *a, void *opaque)
{
    (void)a;
    struct pipe_ctx *ctx = opaque;
    archive_read_free(ctx->source);
    free(ctx);
    return ARCHIVE_OK;
}

/*
 * Normalize a path in-place by stripping leading "./", collapsing "//",
 * and resolving "." and ".." components.  Returns a newly allocated string.
 */
static char *normalize_path(const char *raw)
{
    int absolute = (raw[0] == '/');
    char *work = aept_strdup(raw);
    char *save = NULL;

    /* Split into components, skipping "." and resolving ".." */
    size_t nparts = 0, cap = 16;
    char **parts = aept_malloc(cap * sizeof(*parts));

    for (char *tok = strtok_r(work, "/", &save); tok;
         tok = strtok_r(NULL, "/", &save)) {
        if (tok[0] == '.' && tok[1] == '\0')
            continue;
        if (tok[0] == '.' && tok[1] == '.' && tok[2] == '\0') {
            if (nparts > 0)
                nparts--;
            continue;
        }
        if (nparts == cap) {
            cap *= 2;
            parts = aept_realloc(parts, cap * sizeof(*parts));
        }
        parts[nparts++] = tok;
    }

    /* Reassemble */
    size_t total = absolute ? 1 : 0;
    for (size_t i = 0; i < nparts; i++)
        total += (i > 0 ? 1 : 0) + strlen(parts[i]);

    char *out = aept_malloc(total + 1);
    char *dst = out;
    if (absolute)
        *dst++ = '/';
    for (size_t i = 0; i < nparts; i++) {
        if (i > 0)
            *dst++ = '/';
        size_t len = strlen(parts[i]);
        memcpy(dst, parts[i], len);
        dst += len;
    }
    *dst = '\0';

    free(parts);
    free(work);
    return out;
}

/*
 * Build a safe destination path by joining a prefix directory and an
 * archive-relative entry path.  Returns NULL (and skips) for "." entries
 * and for paths that escape the prefix via "..".
 */
static char *safe_join(const char *prefix, const char *entry_path)
{
    /* Strip leading "./" or "/" from the entry path */
    while (entry_path[0] == '.' && entry_path[1] == '/')
        entry_path += 2;
    while (entry_path[0] == '/')
        entry_path++;

    /* Bare "." means the root directory itself — skip */
    if (entry_path[0] == '\0')
        return NULL;
    if (entry_path[0] == '.' && entry_path[1] == '\0')
        return NULL;

    if (!prefix)
        return aept_strdup(entry_path);

    /* Trim trailing slashes from prefix */
    size_t plen = strlen(prefix);
    while (plen > 1 && prefix[plen - 1] == '/')
        plen--;

    char *combined;
    aept_asprintf(&combined, "%.*s/%s", (int)plen, prefix, entry_path);

    char *resolved = normalize_path(combined);
    free(combined);

    /* Verify the result still starts with the normalized prefix */
    char *norm_pfx = normalize_path(prefix);
    size_t nplen = strlen(norm_pfx);

    if (strncmp(resolved, norm_pfx, nplen) != 0 ||
            (resolved[nplen] != '/' && resolved[nplen] != '\0')) {
        aept_log_error("path '%s' escapes extraction directory", entry_path);
        free(resolved);
        free(norm_pfx);
        return NULL;
    }

    free(norm_pfx);
    return resolved;
}

/* Rewrite the pathname of an entry.  Returns 0 on success, 1 to skip. */
static int rewrite_pathname(struct archive_entry *entry, const char *dest)
{
    char *p = safe_join(dest, archive_entry_pathname(entry));
    if (!p)
        return 1;
    archive_entry_set_pathname(entry, p);
    free(p);
    return 0;
}

/* Rewrite both pathname and hardlink target.  Returns 0/1/-1. */
static int rewrite_all_paths(struct archive_entry *entry, const char *dest)
{
    if (rewrite_pathname(entry, dest))
        return 1;

    const char *hl = archive_entry_hardlink(entry);
    if (hl) {
        char *p = safe_join(dest, hl);
        if (!p) {
            aept_log_error("not extracting '%s': hardlink to nowhere",
                      archive_entry_pathname(entry));
            return 1;
        }
        archive_entry_set_hardlink(entry, p);
        free(p);
    }
    return 0;
}

/*
 * Read the next header from an archive, with basic retry logic.
 * Sets *eof=1 when the archive is exhausted.  Returns NULL on EOF or error.
 */
static struct archive_entry *next_header(struct archive *ar, int *eof)
{
    struct archive_entry *entry;

    if (eof)
        *eof = 0;

    for (int attempt = 0; attempt < 4; attempt++) {
        int r = archive_read_next_header(ar, &entry);
        switch (r) {
        case ARCHIVE_OK:
            return entry;
        case ARCHIVE_WARN:
            aept_log_debug("archive header warning: %s",
                      archive_error_string(ar));
            return entry;
        case ARCHIVE_EOF:
            if (eof)
                *eof = 1;
            return NULL;
        case ARCHIVE_RETRY:
            aept_log_error("archive header error (retry): %s",
                      archive_error_string(ar));
            continue;
        default:
            aept_log_error("archive header error: %s",
                      archive_error_string(ar));
            return NULL;
        }
    }

    return NULL;
}

/*
 * Copy all remaining data from the current archive entry to a stdio stream.
 */
static int stream_entry(struct archive *ar, FILE *fp)
{
    char buf[BLOCK_SIZE];

    if (archive_format(ar) == ARCHIVE_FORMAT_EMPTY)
        return 0;

    for (;;) {
        ssize_t n = archive_read_data(ar, buf, sizeof(buf));
        if (n == 0)
            return 0;
        if (n < 0) {
            aept_log_error("failed to read archive data: %s",
                      archive_error_string(ar));
            return -1;
        }
        if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n) {
            aept_log_error("failed to write to stream: %s",
                      strerror(errno));
            return -1;
        }
    }
}

/*
 * Open the outer .ipk container (an AR or gzipped-tar archive).
 */
static struct archive *open_outer(const char *path)
{
    struct archive *ar = archive_read_new();
    if (!ar) {
        aept_log_error("failed to create archive reader");
        return NULL;
    }

    archive_read_support_format_ar(ar);
    archive_read_support_format_tar(ar);
    archive_read_support_filter_gzip(ar);

    if (archive_read_open_filename(ar, path, BLOCK_SIZE) != ARCHIVE_OK) {
        aept_log_error("failed to open '%s': %s",
                  path, archive_error_string(ar));
        archive_read_free(ar);
        return NULL;
    }

    return ar;
}

/*
 * Scan AR members for an entry whose name starts with the given prefix
 * (e.g. "control.tar" or "data.tar").  Positions the reader on that
 * member so its data can be piped into an inner reader.
 */
static int seek_member(struct archive *ar, const char *prefix)
{
    size_t pfxlen = strlen(prefix);

    for (;;) {
        struct archive_entry *entry = next_header(ar, NULL);
        if (!entry)
            return -1;

        const char *name = archive_entry_pathname(entry);

        /* Strip leading "./" that some AR writers prepend */
        if (name[0] == '.' && name[1] == '/')
            name += 2;

        if (strncmp(name, prefix, pfxlen) == 0)
            return 0;
    }
}

/*
 * Open the inner compressed tar that is embedded in an AR member.
 * Takes ownership of `outer` (freed on close of the returned reader).
 */
static struct archive *open_inner(struct archive *outer)
{
    struct pipe_ctx *ctx = aept_malloc(sizeof(*ctx));
    ctx->source = outer;

    struct archive *inner = archive_read_new();
    if (!inner) {
        aept_log_error("failed to create inner archive reader");
        free(ctx);
        return NULL;
    }

    archive_read_support_filter_all(inner);
    archive_read_support_format_tar(inner);
    archive_read_support_format_empty(inner);

    if (archive_read_open(inner, ctx, NULL, pipe_read_cb,
                          pipe_close_cb) != ARCHIVE_OK) {
        aept_log_error("failed to open inner archive: %s",
                  archive_error_string(inner));
        archive_read_free(inner);
        free(ctx);
        return NULL;
    }

    return inner;
}

/*
 * Open an inner tar from an IPK, seeking to the AR member whose name
 * starts with `prefix` (e.g. "control.tar" or "data.tar").
 */
static struct archive *open_ipk_tar(const char *ipk_path, const char *prefix)
{
    struct archive *outer = open_outer(ipk_path);
    if (!outer)
        return NULL;

    if (seek_member(outer, prefix) < 0) {
        archive_read_free(outer);
        return NULL;
    }

    struct archive *inner = open_inner(outer);
    if (!inner)
        return NULL;  /* outer freed by open_inner on failure path? No. */

    return inner;
}

/*
 * Create a disk-writer for extraction with the given flags.
 */
static struct archive *new_disk_writer(int flags)
{
    struct archive *disk = archive_write_disk_new();
    if (!disk) {
        aept_log_error("failed to create disk writer");
        return NULL;
    }

    if (archive_write_disk_set_options(disk, flags) != ARCHIVE_OK &&
            archive_write_disk_set_options(disk, flags) != ARCHIVE_WARN) {
        aept_log_error("failed to set disk options: %s",
                  archive_error_string(disk));
        archive_write_free(disk);
        return NULL;
    }

    archive_write_disk_set_standard_lookup(disk);
    return disk;
}

/*
 * Extract every entry from `ar` into `dest`, using the given flags.
 * If `conffiles` is non-empty, matching entries are extracted with
 * `cf_suffix` appended to the destination (e.g. ".aept-new") and
 * without the NO_OVERWRITE flag.
 */
static int do_extract_all(struct archive *ar, const char *dest, int flags,
                          unsigned long *size, aept_fileset_t *conffiles,
                          const char *cf_suffix)
{
    int ret = -1;

    struct archive *disk = new_disk_writer(flags);
    if (!disk)
        return -1;

    int have_cf = conffiles && conffiles->count > 0;
    struct archive *cf_disk = NULL;
    if (have_cf) {
        cf_disk = new_disk_writer(flags & ~ARCHIVE_EXTRACT_NO_OVERWRITE);
        if (!cf_disk) {
            archive_write_free(disk);
            return -1;
        }
    }

    for (;;) {
        int eof;
        struct archive_entry *entry = next_header(ar, &eof);
        if (eof)
            break;
        if (!entry)
            goto cleanup;

        int is_cf = have_cf &&
            aept_fileset_contains(conffiles, archive_entry_pathname(entry));

        int skip = rewrite_all_paths(entry, dest);
        if (skip == 1)
            continue;
        if (skip < 0)
            goto cleanup;

        if (is_cf && cf_suffix) {
            const char *pathname = archive_entry_pathname(entry);
            char *suffixed;
            aept_asprintf(&suffixed, "%s%s", pathname, cf_suffix);
            archive_entry_set_pathname(entry, suffixed);
            free(suffixed);
        }

        aept_log_debug("extracting '%s'", archive_entry_pathname(entry));

        int r = archive_read_extract2(ar, entry, is_cf ? cf_disk : disk);
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
            aept_log_error("failed to extract '%s': %s",
                      archive_entry_pathname(entry),
                      archive_error_string(ar));
            goto cleanup;
        }
        if (r == ARCHIVE_WARN)
            aept_log_debug("warning extracting '%s': %s",
                      archive_entry_pathname(entry),
                      archive_error_string(ar));

        if (size)
            *size += archive_entry_size(entry);
    }

    ret = 0;
cleanup:
    if (cf_disk)
        archive_write_free(cf_disk);
    archive_write_free(disk);
    return ret;
}

/*
 * Public API
 */

struct aept_ar *aept_ar_open_pkg_control_archive(const char *filename)
{
    struct archive *inner = open_ipk_tar(filename, "control.tar");
    if (!inner)
        return NULL;

    struct aept_ar *ar = aept_malloc(sizeof(*ar));
    ar->ar = inner;
    ar->extract_flags = ARCHIVE_EXTRACT_SECURE_SYMLINKS |
        ARCHIVE_EXTRACT_SECURE_NODOTDOT;

    return ar;
}

struct aept_ar *aept_ar_open_pkg_data_archive(const char *filename)
{
    struct archive *inner = open_ipk_tar(filename, "data.tar");
    if (!inner)
        return NULL;

    struct aept_ar *ar = aept_malloc(sizeof(*ar));
    ar->ar = inner;
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
    struct archive *reader = archive_read_new();
    if (!reader) {
        aept_log_error("failed to create archive reader for compressed file");
        return NULL;
    }

    archive_read_support_filter_gzip(reader);
    archive_read_support_format_raw(reader);
    archive_read_support_format_empty(reader);

    if (archive_read_open_filename(reader, filename,
                                   BLOCK_SIZE) != ARCHIVE_OK) {
        aept_log_error("failed to open '%s': %s",
                  filename, archive_error_string(reader));
        archive_read_free(reader);
        return NULL;
    }

    /* Advance past the synthetic header for raw format */
    int eof;
    struct archive_entry *entry = next_header(reader, &eof);
    if (!entry && !eof) {
        archive_read_free(reader);
        return NULL;
    }

    struct aept_ar *ar = aept_malloc(sizeof(*ar));
    ar->ar = reader;
    ar->extract_flags = 0;
    return ar;
}

int aept_ar_copy_to_stream(struct aept_ar *ar, FILE *stream)
{
    return stream_entry(ar->ar, stream);
}

int aept_ar_extract_file_to_stream(struct aept_ar *ar, const char *filename,
                              FILE *stream)
{
    for (;;) {
        struct archive_entry *entry = next_header(ar->ar, NULL);
        if (!entry)
            return -1;

        rewrite_pathname(entry, NULL);

        if (strcmp(archive_entry_pathname(entry), filename) == 0)
            return stream_entry(ar->ar, stream);
    }
}

int aept_ar_extract_paths_to_stream(struct aept_ar *ar, FILE *stream)
{
    for (;;) {
        int eof;
        struct archive_entry *entry = next_header(ar->ar, &eof);
        if (eof)
            return 0;
        if (!entry)
            return -1;

        const char *path = archive_entry_pathname(entry);

        if (!aept_archive_path_is_safe(path)) {
            aept_log_error("refusing unsafe archive path '%s'", path);
            return -1;
        }

        const struct stat *st = archive_entry_stat(entry);
        int r;
        if (S_ISLNK(st->st_mode)) {
            const char *target = archive_entry_symlink(entry);
            if (!aept_symlink_target_is_safe(target))
                target = "<redacted>";
            r = fprintf(stream, "%s\t%#03o\t%s\n", path,
                        (unsigned int)st->st_mode, target);
        } else {
            r = fprintf(stream, "%s\t%#03o\n", path,
                        (unsigned int)st->st_mode);
        }
        if (r <= 0) {
            aept_log_error("failed to write path to stream: %s",
                      strerror(errno));
            return -1;
        }
    }
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

int aept_ar_list_data_paths(const char *ipk_path, aept_ar_file_list_t *out)
{
    struct aept_ar *ar = aept_ar_open_pkg_data_archive(ipk_path);
    if (!ar)
        return -1;

    for (;;) {
        int eof;
        struct archive_entry *entry = next_header(ar->ar, &eof);
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

        /* Grow the list if needed */
        if (out->count >= out->alloc) {
            out->alloc = out->alloc ? out->alloc * 2 : 64;
            out->entries = aept_realloc(out->entries,
                                    out->alloc * sizeof(aept_ar_file_entry_t));
        }

        out->entries[out->count].path = aept_strdup(path);
        out->entries[out->count].link_target =
            target ? aept_strdup(target) : NULL;
        out->count++;
    }

    aept_ar_close(ar);
    return 0;
}

int aept_ar_extract_all(struct aept_ar *ar, const char *prefix,
                   unsigned long *size, aept_fileset_t *conffiles,
                   const char *cf_suffix)
{
    return do_extract_all(ar->ar, prefix, ar->extract_flags, size,
                          conffiles, cf_suffix);
}

int aept_ar_extract_selected(struct aept_ar *ar, aept_fileset_t *selected,
                        const char *prefix)
{
    int flags = ar->extract_flags & ~ARCHIVE_EXTRACT_NO_OVERWRITE;
    int ret = -1;

    struct archive *disk = new_disk_writer(flags);
    if (!disk)
        return -1;

    for (;;) {
        int eof;
        struct archive_entry *entry = next_header(ar->ar, &eof);
        if (eof)
            break;
        if (!entry)
            goto cleanup;

        if (!aept_fileset_contains(selected, archive_entry_pathname(entry)))
            continue;

        int skip = rewrite_all_paths(entry, prefix);
        if (skip == 1)
            continue;
        if (skip < 0)
            goto cleanup;

        aept_log_debug("extracting conffile '%s'",
                  archive_entry_pathname(entry));

        int r = archive_read_extract2(ar->ar, entry, disk);
        if (r != ARCHIVE_OK && r != ARCHIVE_WARN) {
            aept_log_error("failed to extract '%s': %s",
                      archive_entry_pathname(entry),
                      archive_error_string(ar->ar));
            goto cleanup;
        }
        if (r == ARCHIVE_WARN)
            aept_log_debug("warning extracting '%s': %s",
                      archive_entry_pathname(entry),
                      archive_error_string(ar->ar));
    }

    ret = 0;
cleanup:
    archive_write_free(disk);
    return ret;
}

void aept_ar_close(struct aept_ar *ar)
{
    archive_read_free(ar->ar);
    free(ar);
}
