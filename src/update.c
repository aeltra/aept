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

#include "libfetch/fetch.h"

#include "aept/internal.h"
#include "aept/archive.h"
#include "aept/clearsign.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/update.h"
#include "aept/util.h"
#include "aept/validator.h"
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
 * Ask for the index only if the copy already held is out of date.
 *
 * Two things have to be true before a request may be conditional: the
 * files that would be kept are all still there, and the validators on
 * record were written for this very url.  Miss either and the answer
 * "unchanged" would leave nothing behind, or leave the wrong thing.
 *
 * Sets *have and returns 1 when the request may carry validators.
 */
static int have_validators(const char *val_path, const char *url, const char *keep_a,
                           const char *keep_b, struct libfetch_validators *have)
{
    memset(have, 0, sizeof(*have));

    if (!aept_file_exists(keep_a))
        return 0;
    if (keep_b && !aept_file_exists(keep_b))
        return 0;

    return aept_validator_load(val_path, url, have) == 0;
}

/*
 * Fetch InPackages.gz — the index and its signature in a single object —
 * split it into the message and signature files that usign expects, and
 * verify it.
 *
 * Fetching one object instead of two removes the window in which a
 * republished repository can hand out an index and a signature that do
 * not belong together.  A repository that requires signature checking
 * must publish it: there is deliberately no fallback to a detached
 * Packages.sig, so that blocking this one request cannot push a client
 * back onto the two-object path.
 *
 * On a 304 nothing is downloaded, nothing is written and nothing is
 * verified: what is on disk was verified when it was put there, which
 * is the whole saving.
 *
 * Returns 0 on success, -1 on error.
 */
static int fetch_signed_index(struct aept_ctx *ctx, aept_source_t *src, const char *list_path,
                              const char *sig_path, const char *val_path, int *unchanged)
{
    char *url = NULL;
    char *tmp_path = NULL;
    char *buf = NULL;
    size_t buf_len = 0;
    struct libfetch_validators have, got;
    aept_clearsign_t cs;
    int conditional;
    int r;

    aept_asprintf(&url, "%s/InPackages.gz", src->url);
    aept_asprintf(&tmp_path, "%s.in.gz", list_path);

    conditional = have_validators(val_path, url, list_path, sig_path, &have);

    r = aept_download_cond(ctx, url, tmp_path, url, src->user, src->password,
                           conditional ? &have : NULL, &got, unchanged);

    if (r < 0) {
        aept_log_error("failed to download InPackages.gz for '%s'; a signed "
                       "repository must publish one",
                       src->name);
        unlink(tmp_path);
        free(url);
        free(tmp_path);
        return -1;
    }

    if (*unchanged) {
        free(url);
        free(tmp_path);
        return 0;
    }

    buf = slurp_gz(tmp_path, &buf_len);
    unlink(tmp_path);
    free(tmp_path);

    if (!buf) {
        aept_log_error("failed to decompress InPackages.gz for '%s'", src->name);
        free(url);
        return -1;
    }

    if (aept_clearsign_parse(buf, buf_len, &cs) < 0) {
        aept_log_error("malformed signed index for '%s'", src->name);
        free(buf);
        free(url);
        return -1;
    }

    /*
     * Written aside, verified there, and only then renamed into place.
     * The index already on disk was verified when it was stored, and a
     * rejected replacement must not cost the client its last good copy:
     * verifying in place would wipe it, which turns "freeze the client"
     * -- the strongest attack a bad signature affords -- into "break
     * the client".  The validator on disk stays untouched for the same
     * reason: it describes the index that survives, not the document
     * that was refused.
     */
    {
        char *new_list = NULL;
        char *new_sig = NULL;

        aept_asprintf(&new_list, "%s.%d", list_path, (int)getpid());
        aept_asprintf(&new_sig, "%s.%d", sig_path, (int)getpid());

        r = aept_clearsign_write(&cs, new_list, new_sig);
        free(buf);

        if (r == 0 && aept_verify_signature(ctx, new_list, new_sig) < 0)
            r = -1;

        /* The signature first: it is never read again once the index
         * is in place, so a crash between the two renames leaves a
         * stale .sig beside a good index rather than the reverse. */
        if (r == 0 && (rename(new_sig, sig_path) != 0 || rename(new_list, list_path) != 0)) {
            aept_log_error("cannot move the verified index for '%s' into place: %s", src->name,
                           strerror(errno));
            r = -1;
        }

        if (r != 0) {
            unlink(new_list);
            unlink(new_sig);
        }
        free(new_list);
        free(new_sig);
    }

    /* Last, and only here: a validator recorded for an index that was
     * then rejected would revalidate against a document this client
     * never accepted, and the 304 it earns would freeze it there. */
    if (r == 0)
        aept_validator_save(val_path, url, &got);

    free(url);
    return r < 0 ? -1 : 0;
}

/* Fetch the plain index, decompressing it when the source is gzipped. */
static int fetch_plain_index(struct aept_ctx *ctx, aept_source_t *src, const char *list_path,
                             const char *val_path, int *unchanged)
{
    char *url = NULL;
    struct libfetch_validators have, got;
    int conditional;
    int r;

    if (src->gzip) {
        char *gz_path = NULL;

        aept_asprintf(&url, "%s/Packages.gz", src->url);
        aept_asprintf(&gz_path, "%s.gz", list_path);

        conditional = have_validators(val_path, url, list_path, NULL, &have);

        r = aept_download_cond(ctx, url, gz_path, url, src->user, src->password,
                               conditional ? &have : NULL, &got, unchanged);

        if (r == 0 && !*unchanged) {
            r = decompress_gz(gz_path, list_path);
            if (r < 0) {
                aept_log_error("failed to decompress Packages.gz for '%s'", src->name);
                unlink(val_path);
            } else {
                aept_validator_save(val_path, url, &got);
            }
        }

        unlink(gz_path);
        free(gz_path);
        free(url);
        return r;
    }

    aept_asprintf(&url, "%s/Packages", src->url);

    conditional = have_validators(val_path, url, list_path, NULL, &have);

    r = aept_download_cond(ctx, url, list_path, "Packages", src->user, src->password,
                           conditional ? &have : NULL, &got, unchanged);

    if (r == 0 && !*unchanged)
        aept_validator_save(val_path, url, &got);

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

/* What a source leaves in the lists directory besides the index itself. */
static const char *const list_suffixes[] = {".sig", ".validator"};

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
        size_t i;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        /* Strip the suffix, if any, to get the source name */
        copy = aept_strdup(name);
        base = copy;

        size_t len = strlen(base);
        for (i = 0; i < sizeof(list_suffixes) / sizeof(list_suffixes[0]); i++) {
            size_t slen = strlen(list_suffixes[i]);

            if (len > slen && strcmp(base + len - slen, list_suffixes[i]) == 0) {
                base[len - slen] = '\0';
                break;
            }
        }

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
        char *val_path = NULL;
        int unchanged = 0;

        if (aept_cancelled()) {
            aept_log_warning("interrupted, stopping");
            errors++;
            break;
        }

        aept_asprintf(&list_path, "%s/%s", ctx->config.lists_dir, src->name);
        aept_asprintf(&sig_path, "%s.sig", list_path);
        aept_asprintf(&val_path, "%s.validator", list_path);

        if (ctx->config.check_signature) {
            if (fetch_signed_index(ctx, src, list_path, sig_path, val_path, &unchanged) < 0) {
                errors++;
                goto next;
            }
        } else if (fetch_plain_index(ctx, src, list_path, val_path, &unchanged) < 0) {
            errors++;
            goto next;
        }

        if (unchanged)
            aept_log_info("source '%s' is already up to date", src->name);
        else
            aept_log_info("updated source '%s'", src->name);

    next:
        free(list_path);
        free(sig_path);
        free(val_path);
    }

    prune_stale_lists(ctx);

    return errors ? -1 : 0;
}
