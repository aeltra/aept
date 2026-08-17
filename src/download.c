/* download.c - HTTP download and package retrieval
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libfetch/fetch.h"
#include <solv/chksum.h>
#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/solvable.h>

#include "aept/internal.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/util.h"

/*
 * Record why a transfer failed, so a caller can tell "the peer went
 * quiet" from "the peer said no".  A timeout is the one an embedding
 * application is likely to want to retry rather than report.
 */
static void record_error(struct aept_ctx *ctx)
{
    if (libfetch_last_error.category == LIBFETCH_ERRCAT_FETCH &&
        libfetch_last_error.code == LIBFETCH_ERR_TIMEOUT)
        ctx->last_error = AEPT_ERR_TIMEOUT;
    else
        ctx->last_error = AEPT_ERR_GENERAL;
}

int aept_download_cond(struct aept_ctx *ctx, const char *url, const char *dest, const char *name,
                       const char *user, const char *password,
                       const struct libfetch_validators *have, struct libfetch_validators *got,
                       int *unchanged)
{
    struct libfetch_url *fu = NULL;
    libfetch_io_t *fio = NULL;
    FILE *fp = NULL;
    char *tmp = NULL;
    /* Everything this function says, it says without the userinfo:
     * url carries the credentials, and name is often the same url. */
    char *shown_url = aept_url_sanitized(url);
    char *shown_name = aept_url_sanitized(name);
    char buf[65536];
    ssize_t n;
    int ret = -1;

    aept_log_info("downloading %s", shown_name);

    if (unchanged)
        *unchanged = 0;

    ctx->last_error = AEPT_ERR_NONE;

    /*
     * Hand the client certificate to this context's fetch state.  It
     * used to go through setenv("SSL_CLIENT_KEY_FILE", ...), which put
     * the path to the private key in the environment of every process
     * aept later forked, maintainer scripts included.  Set
     * unconditionally: passing NULL clears any earlier selection.
     */
    libfetch_set_client_certificate(ctx->http, ctx->config.ssl_client_cert,
                                    ctx->config.ssl_client_key);

    /*
     * Parse, inject, fetch -- libfetch_get_url() minus the scheme
     * dispatch, which the parser already performs: it accepts nothing
     * but http and https.  The credentials enter here, after the parse,
     * so they exist in no url string anywhere in aept; a url that
     * carries its own userinfo (a caller going through aept_download()
     * directly) still works, since the parse fills the same two fields
     * and the injection only overrides what it was given.
     */
    fu = libfetch_parse_url(url);
    if (fu) {
        if (user)
            snprintf(fu->user, sizeof(fu->user), "%s", user);
        if (password)
            snprintf(fu->pwd, sizeof(fu->pwd), "%s", password);
        fio = libfetch_get_http(ctx->http, fu, "", have, got);
        libfetch_free_url(fu);
    }
    if (!fio) {
        /*
         * "Still current" is an answer, not a failure, and there is no
         * body to write: what is on disk stays, already verified when
         * it was put there.  libfetch only reports this in reply to a
         * request that carried a validator -- an unsolicited 304 never
         * reaches here, it is a protocol error like an unsolicited 206.
         */
        if (libfetch_last_error.category == LIBFETCH_ERRCAT_HTTP &&
            libfetch_last_error.code == LIBFETCH_HTTP_NOT_MODIFIED) {
            aept_log_debug("%s is unchanged", shown_name);
            *unchanged = 1;
            ret = 0;
            goto cleanup;
        }

        record_error(ctx);
        if (ctx->last_error == AEPT_ERR_TIMEOUT)
            aept_log_error("timed out downloading '%s'", shown_url);
        else
            aept_log_error("failed to download '%s'", shown_url);
        goto cleanup;
    }

    /* Download to <dest>.<pid>, then rename into place. This ensures
     * readers never see a partially-written file, even when multiple
     * aept instances share the same download cache. */
    aept_asprintf(&tmp, "%s.%d", dest, (int)getpid());

    fp = fopen(tmp, "wb");
    if (!fp) {
        aept_log_error("cannot create '%s': %s", tmp, strerror(errno));
        goto cleanup;
    }

    for (;;) {
        n = libfetch_io_read(fio, buf, sizeof(buf));
        if (aept_cancelled())
            goto cleanup;
        if (n == 0)
            break;
        if (n < 0) {
            /*
             * A signal, not a failure: nothing was transferred and the
             * stream is intact, so read again.  Cancellation is looked
             * at first, above, which is what keeps this from swallowing
             * the one signal aept is meant to act on -- and it is the
             * reason libfetch may report an interruption instead of
             * deciding for itself to retry.
             */
            if (errno == EINTR)
                continue;
            record_error(ctx);
            if (ctx->last_error == AEPT_ERR_TIMEOUT)
                aept_log_error("timed out downloading '%s'", shown_url);
            else
                aept_log_error("failed to download '%s'", shown_url);
            goto cleanup;
        }
        if (fwrite(buf, 1, n, fp) != (size_t)n) {
            aept_log_error("write error for '%s': %s", tmp, strerror(errno));
            goto cleanup;
        }
    }

    if (fclose(fp) != 0) {
        aept_log_error("write error for '%s': %s", tmp, strerror(errno));
        fp = NULL;
        goto cleanup;
    }
    fp = NULL;

    if (rename(tmp, dest) != 0) {
        aept_log_error("rename '%s' -> '%s': %s", tmp, dest, strerror(errno));
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (fio)
        libfetch_io_close(fio);
    if (fp)
        fclose(fp);
    if (ret != 0 && tmp)
        unlink(tmp);
    free(tmp);
    free(shown_url);
    free(shown_name);
    return ret;
}

int aept_download(struct aept_ctx *ctx, const char *url, const char *dest, const char *name)
{
    return aept_download_cond(ctx, url, dest, name, NULL, NULL, NULL, NULL, NULL);
}

static int verify_checksum(const char *path, Pool *pool, Solvable *s)
{
    Id checksum_type;
    const unsigned char *expected;
    Chksum *chk;
    FILE *fp;
    char buf[4096];
    size_t n;
    const unsigned char *computed;
    int len;
    const char *name = pool_id2str(pool, s->name);

    expected = solvable_lookup_bin_checksum(s, SOLVABLE_CHECKSUM, &checksum_type);
    if (!expected) {
        aept_log_error("no checksum for '%s'", name);
        return -1;
    }

    chk = solv_chksum_create(checksum_type);
    if (!chk) {
        aept_log_error("unsupported checksum type for '%s'", name);
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        aept_log_error("cannot open '%s' for checksum verification: %s", path, strerror(errno));
        solv_chksum_free(chk, NULL);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        solv_chksum_add(chk, buf, (int)n);

    fclose(fp);

    computed = solv_chksum_get(chk, &len);

    if (len != solv_chksum_len(checksum_type) || memcmp(computed, expected, len) != 0) {
        aept_log_error("%s checksum mismatch for '%s'", solv_chksum_type2str(checksum_type), name);
        solv_chksum_free(chk, NULL);
        unlink(path);
        return -1;
    }

    solv_chksum_free(chk, NULL);
    return 0;
}

int aept_download_package(struct aept_ctx *ctx, Id p, Pool *pool, char **dest_out)
{
    Solvable *s = pool_id2solvable(pool, p);
    unsigned int medianr;
    const char *location = solvable_lookup_location(s, &medianr);
    int src_idx;
    char *url = NULL;
    char *dest = NULL;
    char *location_copy = NULL;
    char *base;
    int r;

    if (!location) {
        aept_log_error("no download location for '%s'", pool_id2str(pool, s->name));
        return -1;
    }

    src_idx = aept_solver_solvable_source_index(ctx->solver, p);
    if (src_idx < 0 || src_idx >= ctx->config.nsources) {
        aept_log_error("unknown source for '%s'", pool_id2str(pool, s->name));
        return -1;
    }

    aept_source_t *source = &ctx->config.sources[src_idx];

    aept_asprintf(&url, "%s/%s", source->url, location);

    location_copy = aept_strdup(location);
    base = basename(location_copy);
    aept_asprintf(&dest, "%s/%s", ctx->config.cache_dir, base);

    aept_file_mkdir_hier(ctx->config.cache_dir, 0755);

    /* Try cached copy first */
    if (access(dest, F_OK) == 0) {
        if (verify_checksum(dest, pool, s) == 0) {
            aept_log_debug("using cached %s", pool_id2str(pool, s->name));
            free(url);
            free(location_copy);
            *dest_out = dest;
            return 0;
        }
        /* checksum failed — verify_checksum already deleted the file */
    }

    r = aept_download_cond(ctx, url, dest, base, source->user, source->password, NULL, NULL, NULL);
    free(url);
    free(location_copy);

    if (r < 0) {
        free(dest);
        return -1;
    }

    r = verify_checksum(dest, pool, s);
    if (r < 0) {
        free(dest);
        return -1;
    }

    *dest_out = dest;
    return 0;
}
