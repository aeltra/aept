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

#include <fetch.h>
#include <solv/chksum.h>
#include <solv/knownid.h>
#include <solv/pool.h>
#include <solv/solvable.h>

#include "aept/internal.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/solver.h"
#include "aept/util.h"

int aept_download(struct aept_ctx *ctx, const char *url, const char *dest,
                  const char *name)
{
    fetchIO *fio = NULL;
    FILE *fp = NULL;
    char *tmp = NULL;
    char buf[65536];
    ssize_t n;
    int ret = -1;

    aept_log_info("downloading %s", name);

    /* Pass client cert config to libfetch via env vars */
    if (ctx->config.ssl_client_cert)
        setenv("SSL_CLIENT_CERT_FILE", ctx->config.ssl_client_cert, 1);
    if (ctx->config.ssl_client_key)
        setenv("SSL_CLIENT_KEY_FILE", ctx->config.ssl_client_key, 1);

    fio = fetchGetURL(url, "");
    if (!fio) {
        aept_log_error("failed to download '%s'", url);
        return -1;
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
        n = fetchIO_read(fio, buf, sizeof(buf));
        if (aept_cancelled())
            goto cleanup;
        if (n == 0)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            aept_log_error("failed to download '%s'", url);
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
        fetchIO_close(fio);
    if (fp)
        fclose(fp);
    if (ret != 0 && tmp)
        unlink(tmp);
    free(tmp);
    return ret;
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

    expected = solvable_lookup_bin_checksum(s, SOLVABLE_CHECKSUM,
                                            &checksum_type);
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
        aept_log_error("cannot open '%s' for checksum verification: %s",
                  path, strerror(errno));
        solv_chksum_free(chk, NULL);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        solv_chksum_add(chk, buf, (int)n);

    fclose(fp);

    computed = solv_chksum_get(chk, &len);

    if (len != solv_chksum_len(checksum_type) ||
            memcmp(computed, expected, len) != 0) {
        aept_log_error("%s checksum mismatch for '%s'",
                  solv_chksum_type2str(checksum_type), name);
        solv_chksum_free(chk, NULL);
        unlink(path);
        return -1;
    }

    solv_chksum_free(chk, NULL);
    return 0;
}

int aept_download_package(struct aept_ctx *ctx, Id p, Pool *pool,
                          char **dest_out)
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
        aept_log_error("no download location for '%s'",
                  pool_id2str(pool, s->name));
        return -1;
    }

    src_idx = aept_solver_solvable_source_index(ctx->solver, p);
    if (src_idx < 0 || src_idx >= ctx->config.nsources) {
        aept_log_error("unknown source for '%s'",
                  pool_id2str(pool, s->name));
        return -1;
    }

    aept_asprintf(&url, "%s/%s", ctx->config.sources[src_idx].url, location);

    location_copy = aept_strdup(location);
    base = basename(location_copy);
    aept_asprintf(&dest, "%s/%s", ctx->config.cache_dir, base);

    aept_file_mkdir_hier(ctx->config.cache_dir, 0755);

    /* Try cached copy first */
    if (access(dest, F_OK) == 0) {
        if (verify_checksum(dest, pool, s) == 0) {
            aept_log_debug("using cached %s",
                     pool_id2str(pool, s->name));
            free(url);
            free(location_copy);
            *dest_out = dest;
            return 0;
        }
        /* checksum failed — verify_checksum already deleted the file */
    }

    r = aept_download(ctx, url, dest, base);
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
