/* httpget.c - minimal driver over aept_download() for the HTTP
 * characterisation tests
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: httpget <url> <outfile> [<url> <outfile> ...]
 *
 * Downloads each URL in turn through one context, so the connection
 * cache is shared between them, and prints "ok" or "fail" per URL.
 * Exits 0 only if every download succeeded.
 *
 * This is not a test: it is the harness test_http.sh drives.  It goes
 * through aept_download() rather than libfetch directly, because that
 * is the boundary whose behaviour must survive the libfetch fork.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include "aept/aept.h"
#include "aept/download.h"

int main(int argc, char **argv)
{
    aept_ctx_t *ctx;
    int failed = 0;
    int i;

    if (argc < 3 || (argc - 1) % 2 != 0) {
        fprintf(stderr, "usage: %s <url> <outfile> [<url> <outfile> ...]\n",
                argv[0]);
        return 2;
    }

    ctx = aept_init();
    if (!ctx) {
        fprintf(stderr, "aept_init failed\n");
        return 2;
    }

    /* Failures are expected here; the caller reads the per-URL result
     * rather than the log. */
    aept_set_verbosity(ctx, AEPT_LOG_ERROR - 1);

    for (i = 1; i + 1 < argc; i += 2) {
        int r = aept_download(ctx, argv[i], argv[i + 1], "test");

        printf("%s\n", r == 0 ? "ok" : "fail");
        fflush(stdout);

        if (r != 0)
            failed = 1;
    }

    aept_cleanup(ctx);
    return failed;
}
