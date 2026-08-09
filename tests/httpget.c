/* httpget.c - minimal driver over aept_download() for the HTTP
 * characterisation tests
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: httpget [-s] <url> <outfile> [<url> <outfile> ...]
 *
 * Downloads each URL in turn and prints "ok" or "fail" per URL.  Exits
 * 0 only if every download succeeded.
 *
 * By default all URLs go through one context, so they share its
 * connection cache.  With -s each URL gets a context of its own, which
 * is how the test tells that two contexts keep their connections --
 * and therefore their client certificates -- apart.
 *
 * This is not a test: it is the harness test_http.sh drives.  It goes
 * through aept_download() rather than libfetch directly, because that
 * is the boundary whose behaviour must survive the libfetch fork.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/download.h"

/* Open a context with logging quiet: failures are expected, and the
 * caller reads the per-URL result rather than the log. */
static aept_ctx_t *open_ctx(void)
{
    aept_ctx_t *ctx = aept_init();

    if (!ctx) {
        fprintf(stderr, "aept_init failed\n");
        exit(2);
    }

    aept_set_verbosity(ctx, AEPT_LOG_ERROR - 1);
    return ctx;
}

int main(int argc, char **argv)
{
    aept_ctx_t *shared = NULL;
    int separate = 0;
    int failed = 0;
    int first = 1;
    int i;

    if (argc > 1 && strcmp(argv[1], "-s") == 0) {
        separate = 1;
        first = 2;
    }

    if (argc - first < 2 || (argc - first) % 2 != 0) {
        fprintf(stderr,
                "usage: %s [-s] <url> <outfile> [<url> <outfile> ...]\n",
                argv[0]);
        return 2;
    }

    if (!separate)
        shared = open_ctx();

    for (i = first; i + 1 < argc; i += 2) {
        aept_ctx_t *ctx = separate ? open_ctx() : shared;
        int r = aept_download(ctx, argv[i], argv[i + 1], "test");

        printf("%s\n", r == 0 ? "ok" : "fail");
        fflush(stdout);

        if (r != 0)
            failed = 1;

        if (separate)
            aept_cleanup(ctx);
    }

    if (shared)
        aept_cleanup(shared);

    return failed;
}
