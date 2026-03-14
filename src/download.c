/* download.c - HTTP download
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fetch.h>

#include "aept/internal.h"
#include "aept/download.h"
#include "aept/msg.h"
#include "aept/util.h"

int aept_download(struct aept_ctx *ctx, const char *url, const char *dest,
                  const char *name)
{
    fetchIO *fio = NULL;
    FILE *fp = NULL;
    char *tmp = NULL;
    char buf[8192];
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

    while ((n = fetchIO_read(fio, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, n, fp) != (size_t)n) {
            aept_log_error("write error for '%s': %s", tmp, strerror(errno));
            goto cleanup;
        }
    }

    if (n < 0) {
        aept_log_error("failed to download '%s'", url);
        goto cleanup;
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
