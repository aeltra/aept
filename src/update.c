/* update.c - fetch package lists from repositories
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/clearsign.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/update.h"
#include "aept/util.h"
#include "aept/verify.h"

/*
 * Ceiling on the decompressed size of a repository index.
 *
 * Both the plain and the signed index arrive as a gzip stream chosen by
 * the remote side, so expanding one without a bound hands a hostile or
 * compromised mirror a disk- and memory-exhaustion primitive: a few
 * hundred kilobytes on the wire expand to as much as the client will
 * take.  64 MiB is far above any plausible real index and still small
 * enough to hold in memory on the devices aept targets.
 */
#define MAX_INDEX_SIZE ((uint64_t)64 * 1024 * 1024)

static int decompress_gz(const char *gz_path, const char *out_path)
{
    struct aept_ar *ar;
    FILE *fp;
    int r;

    ar = aept_ar_open_compressed_file(gz_path);
    if (!ar)
        return -1;

    fp = fopen(out_path, "w");
    if (!fp) {
        aept_ar_close(ar);
        return -1;
    }

    r = aept_ar_copy_to_stream(ar, fp, MAX_INDEX_SIZE);

    if (fclose(fp) != 0 && r == 0)
        r = -1;

    aept_ar_close(ar);

    /* Hitting the ceiling leaves a truncated index behind; so does any
     * other read error partway through.  Do not keep it. */
    if (r < 0)
        unlink(out_path);

    return r;
}

/* Decompress a gzip file into a newly allocated buffer. */
static char *slurp_gz(const char *path, size_t *out_len)
{
    struct aept_ar *ar;
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *mem;
    int r;

    ar = aept_ar_open_compressed_file(path);
    if (!ar)
        return NULL;

    mem = open_memstream(&buf, &buf_len);
    if (!mem) {
        aept_ar_close(ar);
        return NULL;
    }

    r = aept_ar_copy_to_stream(ar, mem, MAX_INDEX_SIZE);
    aept_ar_close(ar);

    if (fclose(mem) != 0 || r < 0) {
        free(buf);
        return NULL;
    }

    *out_len = buf_len;
    return buf;
}

/*
 * Fetch InPackages.gz — the index and its signature in a single object —
 * and split it into the message and signature files that usign expects.
 *
 * Fetching one object instead of two removes the window in which a
 * republished repository can hand out an index and a signature that do
 * not belong together.  A repository that requires signature checking
 * must publish it: there is deliberately no fallback to a detached
 * Packages.sig, so that blocking this one request cannot push a client
 * back onto the two-object path.
 *
 * Returns 0 on success, -1 on error.
 */
static int fetch_signed_index(struct aept_ctx *ctx, aept_source_t *src, const char *list_path,
                              const char *sig_path)
{
    char *url = NULL;
    char *tmp_path = NULL;
    char *buf = NULL;
    size_t buf_len = 0;
    aept_clearsign_t cs;
    int r;

    aept_asprintf(&url, "%s/InPackages.gz", src->url);
    aept_asprintf(&tmp_path, "%s.in.gz", list_path);

    r = aept_download(ctx, url, tmp_path, url);
    free(url);

    if (r < 0) {
        aept_log_error("failed to download InPackages.gz for '%s'; a signed "
                       "repository must publish one",
                       src->name);
        unlink(tmp_path);
        free(tmp_path);
        return -1;
    }

    buf = slurp_gz(tmp_path, &buf_len);
    unlink(tmp_path);
    free(tmp_path);

    if (!buf) {
        aept_log_error("failed to decompress InPackages.gz for '%s'", src->name);
        return -1;
    }

    if (aept_clearsign_parse(buf, buf_len, &cs) < 0) {
        aept_log_error("malformed signed index for '%s'", src->name);
        free(buf);
        return -1;
    }

    r = aept_clearsign_write(&cs, list_path, sig_path);
    free(buf);

    return r < 0 ? -1 : 0;
}

/* Fetch the plain index, decompressing it when the source is gzipped. */
static int fetch_plain_index(struct aept_ctx *ctx, aept_source_t *src, const char *list_path)
{
    char *url = NULL;
    int r;

    if (src->gzip) {
        char *gz_path = NULL;

        aept_asprintf(&url, "%s/Packages.gz", src->url);
        aept_asprintf(&gz_path, "%s.gz", list_path);

        r = aept_download(ctx, url, gz_path, url);
        free(url);

        if (r == 0) {
            r = decompress_gz(gz_path, list_path);
            if (r < 0)
                aept_log_error("failed to decompress Packages.gz for '%s'", src->name);
        }

        unlink(gz_path);
        free(gz_path);
        return r;
    }

    aept_asprintf(&url, "%s/Packages", src->url);
    r = aept_download(ctx, url, list_path, "Packages");
    free(url);

    return r;
}

static int is_active_source(struct aept_ctx *ctx, const char *name)
{
    int i;

    for (i = 0; i < ctx->config.nsources; i++) {
        if (strcmp(name, ctx->config.sources[i].name) == 0)
            return 1;
    }

    return 0;
}

static void prune_stale_lists(struct aept_ctx *ctx)
{
    DIR *d;
    struct dirent *ent;

    d = opendir(ctx->config.lists_dir);
    if (!d)
        return;

    while ((ent = readdir(d)) != NULL) {
        char *path = NULL;
        const char *name = ent->d_name;
        char *base;
        char *copy;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        /* Strip .sig suffix to get the source name */
        copy = aept_strdup(name);
        base = copy;

        size_t len = strlen(base);
        if (len > 4 && strcmp(base + len - 4, ".sig") == 0)
            base[len - 4] = '\0';

        if (!is_active_source(ctx, base)) {
            aept_asprintf(&path, "%s/%s", ctx->config.lists_dir, name);
            unlink(path);
            free(path);
        }

        free(copy);
    }

    closedir(d);
}

int aept_op_update(struct aept_ctx *ctx)
{
    int i;
    int errors = 0;

    aept_file_mkdir_hier(ctx->config.lists_dir, 0755);

    for (i = 0; i < ctx->config.nsources; i++) {
        if (strncmp(ctx->config.sources[i].url, "https://", 8) != 0)
            aept_log_warning("source '%s' uses insecure transport", ctx->config.sources[i].name);
    }

    for (i = 0; i < ctx->config.nsources; i++) {
        aept_source_t *src = &ctx->config.sources[i];
        char *list_path = NULL;
        char *sig_path = NULL;

        if (aept_cancelled()) {
            aept_log_warning("interrupted, stopping");
            errors++;
            break;
        }

        aept_asprintf(&list_path, "%s/%s", ctx->config.lists_dir, src->name);
        aept_asprintf(&sig_path, "%s.sig", list_path);

        if (ctx->config.check_signature) {
            if (fetch_signed_index(ctx, src, list_path, sig_path) < 0) {
                errors++;
                goto next;
            }

            if (aept_verify_signature(ctx, list_path, sig_path) < 0) {
                unlink(list_path);
                unlink(sig_path);
                errors++;
                goto next;
            }
        } else if (fetch_plain_index(ctx, src, list_path) < 0) {
            errors++;
            goto next;
        }

        aept_log_info("updated source '%s'", src->name);

    next:
        free(list_path);
        free(sig_path);
    }

    prune_stale_lists(ctx);

    return errors ? -1 : 0;
}
