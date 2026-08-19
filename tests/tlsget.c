/* tlsget.c - fetch one URL and say exactly why it failed
 *
 * A harness for test_tls_reject.sh, going to libfetch directly: the
 * point is the *classification* in libfetch_last_error -- which
 * map_tls_error() produces and aept_download() flattens into a log
 * line -- so the shell test needs it spelled out.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include "libfetch/fetch.h"

int main(int argc, char **argv)
{
    struct libfetch_ctx *ctx;
    libfetch_io_t *f;
    const char *flags = "";
    char buf[4096];
    int i = 1;

    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        flags = "v";
        i = 2;
    }
    if (argc - i != 1) {
        fprintf(stderr, "usage: %s [-v] <url>\n", argv[0]);
        return 2;
    }

    ctx = libfetch_ctx_new(4, 2);
    if (!ctx) {
        fprintf(stderr, "libfetch_ctx_new failed\n");
        return 2;
    }

    f = libfetch_get_url(ctx, argv[i], flags, NULL, NULL);
    if (!f) {
        const char *what = "other";

        if (libfetch_last_error.category == LIBFETCH_ERRCAT_TLS) {
            switch (libfetch_last_error.code) {
            case LIBFETCH_ERR_TLS_SERVER_CERT_UNTRUSTED:
                what = "tls-untrusted";
                break;
            case LIBFETCH_ERR_TLS_SERVER_CERT_HOSTNAME:
                what = "tls-hostname";
                break;
            case LIBFETCH_ERR_TLS_HANDSHAKE:
                what = "tls-handshake";
                break;
            default:
                what = "tls-other";
                break;
            }
        }
        printf("fail %s\n", what);
        libfetch_ctx_free(ctx);
        return 1;
    }

    while (libfetch_io_read(f, buf, sizeof(buf)) > 0)
        ;
    libfetch_io_close(f);
    libfetch_ctx_free(ctx);
    printf("ok\n");
    return 0;
}
