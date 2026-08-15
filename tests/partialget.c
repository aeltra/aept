/* partialget.c - walk away from a response part-read, then reuse the
 * context
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: partialget <url-to-abandon> <bytes> <url-to-check> <outfile>
 *
 * Reads exactly <bytes> of the first URL and closes it mid-body, then
 * fetches the second URL through the same context -- and therefore the
 * same connection cache -- writing its body to <outfile> and printing
 * "ok" or "fail".  The count is exact so that the test can say what is
 * left on the wire.
 *
 * A connection abandoned mid-response still has the rest of that
 * response on it, so caching it means the next request is answered by
 * somebody else's body.  aept_download() cannot express this: it reads
 * every transfer to the end, so the harness goes to libfetch directly.
 *
 * This is not a test: it is the harness test_http.sh drives.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include "libfetch/fetch.h"

int main(int argc, char **argv)
{
    struct libfetch_ctx *ctx;
    libfetch_io_t *f;
    FILE *out;
    char buf[512];
    long want;
    long got = 0;
    ssize_t n;

    if (argc != 5) {
        fprintf(stderr, "usage: %s <url-to-abandon> <bytes> <url-to-check> <outfile>\n", argv[0]);
        return 2;
    }

    want = atol(argv[2]);
    if (want <= 0 || (size_t)want > sizeof(buf)) {
        fprintf(stderr, "<bytes> must be between 1 and %zu\n", sizeof(buf));
        return 2;
    }

    ctx = libfetch_ctx_new(4, 2);
    if (!ctx) {
        fprintf(stderr, "libfetch_ctx_new failed\n");
        return 2;
    }

    f = libfetch_get_url(ctx, argv[1], "", NULL, NULL);
    if (!f) {
        fprintf(stderr, "the first request failed\n");
        return 2;
    }
    /* Exactly the requested count: one read may return less. */
    while (got < want) {
        n = libfetch_io_read(f, buf + got, (size_t)(want - got));
        if (n <= 0) {
            fprintf(stderr, "the first response ran out after %ld bytes\n", got);
            return 2;
        }
        got += n;
    }
    libfetch_io_close(f);

    f = libfetch_get_url(ctx, argv[3], "", NULL, NULL);
    if (!f) {
        printf("fail\n");
        return 1;
    }

    out = fopen(argv[4], "wb");
    if (!out) {
        fprintf(stderr, "cannot write '%s'\n", argv[4]);
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

    printf("%s\n", n < 0 ? "fail" : "ok");
    libfetch_ctx_free(ctx);
    return n < 0 ? 1 : 0;
}
