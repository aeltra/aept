/* rawget.c - fetch URLs through libfetch with caller-chosen flags
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: rawget <flags> <url> <outfile> [<url> <outfile> ...]
 *
 * Downloads each URL in turn through one shared context and prints
 * "ok" or "fail" per URL; exits 0 only if every download succeeded.
 * <flags> is handed to libfetch verbatim, "-" meaning none.
 *
 * The flags argument is what httpget cannot express: aept_download()
 * always passes none, so the verbose path -- and with it the drain
 * that reads an error body to its end so the connection can be reused
 * -- is reachable only from here.  So are the '4'/'6' address-family
 * selectors and 'A', which forbids following a redirect.
 *
 * This is not a test: it is a harness test_http.sh drives.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libfetch/fetch.h"

int main(int argc, char **argv)
{
    struct libfetch_ctx *ctx;
    const char *flags;
    char buf[512];
    int failed = 0;
    int i;

    if (argc < 4 || (argc - 2) % 2 != 0) {
        fprintf(stderr, "usage: %s <flags> <url> <outfile> [<url> <outfile> ...]\n", argv[0]);
        return 2;
    }

    flags = strcmp(argv[1], "-") == 0 ? "" : argv[1];

    ctx = libfetch_ctx_new(4, 2);
    if (!ctx) {
        fprintf(stderr, "libfetch_ctx_new failed\n");
        return 2;
    }

    for (i = 2; i + 1 < argc; i += 2) {
        libfetch_io_t *f = libfetch_get_url(ctx, argv[i], flags, NULL, NULL);
        FILE *out = NULL;
        ssize_t n = -1;

        if (f) {
            out = fopen(argv[i + 1], "wb");
            if (!out) {
                fprintf(stderr, "cannot write '%s'\n", argv[i + 1]);
                return 2;
            }
            while ((n = libfetch_io_read(f, buf, sizeof(buf))) > 0) {
                if (fwrite(buf, 1, (size_t)n, out) != (size_t)n) {
                    fprintf(stderr, "write error\n");
                    return 2;
                }
            }
            libfetch_io_close(f);
            if (fclose(out) != 0) {
                fprintf(stderr, "write error\n");
                return 2;
            }
        }

        /* A body that failed mid-read leaves a partial file; drop it
         * so the caller's absence checks mean something. */
        if (!f || n < 0) {
            unlink(argv[i + 1]);
            printf("fail\n");
            failed = 1;
        } else {
            printf("ok\n");
        }
        fflush(stdout);
    }

    libfetch_ctx_free(ctx);
    return failed;
}
