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

int aept_download(const char *url, const char *dest, const char *name)
{
    fetchIO *fio = NULL;
    FILE *fp = NULL;
    char buf[8192];
    ssize_t n;
    int ret = -1;

    aept_log_info("downloading %s", name);

    /* Pass client cert config to libfetch via env vars */
    if (aept_cfg->ssl_client_cert)
        setenv("SSL_CLIENT_CERT_FILE", aept_cfg->ssl_client_cert, 1);
    if (aept_cfg->ssl_client_key)
        setenv("SSL_CLIENT_KEY_FILE", aept_cfg->ssl_client_key, 1);

    unlink(dest);

    fio = fetchGetURL(url, "");
    if (!fio) {
        aept_log_error("failed to download '%s'", url);
        return -1;
    }

    fp = fopen(dest, "wb");
    if (!fp) {
        aept_log_error("cannot create '%s': %s", dest, strerror(errno));
        goto cleanup;
    }

    while ((n = fetchIO_read(fio, buf, sizeof(buf))) > 0) {
        if (fwrite(buf, 1, n, fp) != (size_t)n) {
            aept_log_error("write error for '%s': %s", dest, strerror(errno));
            goto cleanup;
        }
    }

    if (n < 0) {
        aept_log_error("failed to download '%s'", url);
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (fio)
        fetchIO_close(fio);
    if (fp)
        fclose(fp);
    if (ret != 0)
        unlink(dest);
    return ret;
}
