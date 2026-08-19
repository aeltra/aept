/* tlsget.c - fetch one URL and say exactly why it failed
 *
 * A harness for test_tls_reject.sh and test_tls_verify.sh, going to
 * libfetch directly: the point is the *classification* in
 * libfetch_last_error -- which map_tls_error() produces and
 * aept_download() flattens into a log line -- so the shell test needs
 * it spelled out.
 *
 * -C names a CA file to verify against instead of the system store,
 * through the seam that exists for exactly this harness; it is what
 * lets a test reach the verified side of TLS at all.  An optional
 * outfile receives the body, so a test can assert the fetch was not
 * just accepted but intact.
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
    const char *ca_file = NULL;
    const char *outfile = NULL;
    FILE *out = NULL;
    char buf[4096];
    int i = 1;

    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-v") == 0) {
            flags = "v";
            i++;
        } else if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) {
            ca_file = argv[i + 1];
            i += 2;
        } else {
            break;
        }
    }
    if (argc - i != 1 && argc - i != 2) {
        fprintf(stderr, "usage: %s [-v] [-C cafile] <url> [outfile]\n", argv[0]);
        return 2;
    }
    if (argc - i == 2)
        outfile = argv[i + 1];

    ctx = libfetch_ctx_new(4, 2);
    if (!ctx) {
        fprintf(stderr, "libfetch_ctx_new failed\n");
        return 2;
    }
    libfetch_set_ca_file(ctx, ca_file);

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

    if (outfile) {
        out = fopen(outfile, "wb");
        if (!out) {
            fprintf(stderr, "cannot write '%s'\n", outfile);
            return 2;
        }
    }

    for (;;) {
        ssize_t n = libfetch_io_read(f, buf, sizeof(buf));

        if (n <= 0) {
            if (n < 0) {
                fprintf(stderr, "read failed mid-body\n");
                return 2;
            }
            break;
        }
        if (out && fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
            fprintf(stderr, "write error\n");
            return 2;
        }
    }
    if (out && fclose(out) != 0) {
        fprintf(stderr, "write error\n");
        return 2;
    }
    libfetch_io_close(f);
    libfetch_ctx_free(ctx);
    printf("ok\n");
    return 0;
}
