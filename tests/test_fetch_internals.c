/* test_fetch_internals.c - the libfetch fork's pure helpers on their
 * edges: parseuint, the default ports, the context limits and the
 * connection cache
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * The cache decides which connection a request rides on, so its
 * matching rules are security-relevant: a URL carrying one user's
 * credentials must never be answered over a connection opened with
 * another's.  Everything here is internal, hidden from the shared
 * object by -fvisibility=hidden; the test links the static archive,
 * where that does not apply.
 */

#include <config.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "libfetch/fetch.h"
#include "libfetch/common.h"

#include "test.h"

/* How many times the cache has decided to close a connection, whether
 * by eviction or by refusing to store it. */
static int closed_count;

static int counting_close(libfetch_conn_t *conn)
{
    closed_count++;
    return libfetch_close(conn);
}

/* A connection that was never connected: a real socket wrapped the way
 * libfetch_reopen() wraps one, with a cache identity parsed from url.
 * Nothing here writes to the socket, so it never has to work. */
static libfetch_conn_t *fake_conn(const char *url, int af)
{
    libfetch_conn_t *conn;
    int sd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (sd == -1)
        return NULL;
    conn = libfetch_reopen(sd);
    if (!conn)
        return NULL;
    conn->cache_url = libfetch_parse_url(url);
    if (!conn->cache_url)
        return NULL;
    conn->cache_af = af;
    return conn;
}

static void check_parseuint(void)
{
    const char *end;
    uintmax_t v;

    v = libfetch_parseuint("255", &end, 10, 255);
    test_ok(v == 255 && *end == '\0', "decimal at the maximum parses");

    v = libfetch_parseuint("ff", &end, 16, 255);
    test_ok(v == 255 && *end == '\0', "hex at the maximum parses");

    v = libfetch_parseuint("42;ext", &end, 16, SIZE_MAX);
    test_ok(v == 0x42 && *end == ';', "parsing stops at the first non-digit");

    v = libfetch_parseuint("100", &end, 16, 255);
    test_ok(v == 0 && (unsigned char)*end == 0xff, "a value past the maximum is an error");

    v = libfetch_parseuint("256", &end, 10, 255);
    test_ok(v == 0 && (unsigned char)*end == 0xff, "the last digit alone overflowing is caught");

    v = libfetch_parseuint("", &end, 10, 255);
    test_ok(v == 0 && (unsigned char)*end == 0xff, "an empty string is an error, not zero");

    v = libfetch_parseuint("f", &end, 10, 255);
    test_ok(v == 0 && (unsigned char)*end == 0xff, "a hex digit under radix 10 is an error");

    v = libfetch_parseuint("18446744073709551615", &end, 10, UINTMAX_MAX);
    test_ok(v == UINTMAX_MAX && *end == '\0', "UINTMAX_MAX itself parses");

    v = libfetch_parseuint("18446744073709551616", &end, 10, UINTMAX_MAX);
    test_ok(v == 0 && (unsigned char)*end == 0xff, "one past UINTMAX_MAX is an error");
}

static void check_default_ports(void)
{
    test_int_eq(libfetch_default_port("http"), HTTP_DEFAULT_PORT, "http port");
    test_int_eq(libfetch_default_port("https"), HTTPS_DEFAULT_PORT, "https port");
    /* A scheme /etc/services has never heard of: the fallback chain
     * runs to its end. */
    test_int_eq(libfetch_default_port("aept-no-such-scheme"), 0, "unknown scheme has no port");
    test_int_eq(libfetch_default_proxy_port("http"), HTTP_DEFAULT_PROXY_PORT, "proxy port");
}

static void check_ctx_limits(void)
{
    struct libfetch_ctx *ctx;

    ctx = libfetch_ctx_new(-1, -1);
    test_ok(ctx && ctx->cache_global_limit == INT_MAX && ctx->cache_per_host_limit == INT_MAX,
            "negative limits mean unlimited");
    libfetch_ctx_free(ctx);

    ctx = libfetch_ctx_new(2, 5);
    test_ok(ctx && ctx->cache_global_limit == 5,
            "a per-host limit above the global one raises the global one");
    libfetch_ctx_free(ctx);

    ctx = libfetch_ctx_new(4, 2);
    test_ok(ctx && ctx->cache_global_limit == 4 && ctx->cache_per_host_limit == 2,
            "ordinary limits are stored as given");
    libfetch_ctx_free(ctx);

    /* Freeing nothing is allowed, like free(NULL). */
    libfetch_ctx_free(NULL);
}

static void check_cache_refusals(void)
{
    struct libfetch_ctx *ctx;
    libfetch_conn_t *conn;

    /* A zero-sized cache stores nothing: the connection is closed on
     * the spot. */
    ctx = libfetch_ctx_new(0, 0);
    conn = fake_conn("http://a.example/", AF_UNSPEC);
    if (!ctx || !conn) {
        test_ok(0, "context and connection for the zero-limit case");
        return;
    }
    closed_count = 0;
    libfetch_cache_put(ctx, conn, counting_close);
    test_int_eq(closed_count, 1, "a zero-limit cache closes instead of storing");
    libfetch_ctx_free(ctx);

    /* A connection with no cache identity cannot be matched later, so
     * it is closed too, whatever the limits. */
    ctx = libfetch_ctx_new(4, 2);
    conn = libfetch_reopen(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!ctx || !conn) {
        test_ok(0, "context and connection for the no-identity case");
        return;
    }
    closed_count = 0;
    libfetch_cache_put(ctx, conn, counting_close);
    test_int_eq(closed_count, 1, "a connection without a cache identity is closed");
    libfetch_ctx_free(ctx);
}

static void check_cache_matching(void)
{
    struct libfetch_ctx *ctx = libfetch_ctx_new(4, 2);
    struct libfetch_url *want;
    libfetch_conn_t *a, *b, *got;

    a = fake_conn("http://user:pw@a.example:8080/x", AF_INET);
    b = fake_conn("http://b.example:8080/x", AF_UNSPEC);
    if (!ctx || !a || !b) {
        test_ok(0, "context and connections for the matching cases");
        return;
    }

    libfetch_cache_put(ctx, a, counting_close);
    libfetch_cache_put(ctx, b, counting_close);

    /* Each mismatch, one field at a time.  The user and password are
     * the ones that matter most: a hit here would ride one identity's
     * connection with another's request. */
    want = libfetch_parse_url("http://user:pw@a.example:8080/x");
    test_ok(want != NULL, "the probe URL parses");

    want->port = 8081;
    test_ok(libfetch_cache_get(ctx, want, AF_INET) == NULL, "a different port does not match");
    want->port = 8080;

    strcpy(want->scheme, "https");
    test_ok(libfetch_cache_get(ctx, want, AF_INET) == NULL, "a different scheme does not match");
    strcpy(want->scheme, "http");

    strcpy(want->host, "c.example");
    test_ok(libfetch_cache_get(ctx, want, AF_INET) == NULL, "a different host does not match");
    strcpy(want->host, "a.example");

    strcpy(want->user, "other");
    test_ok(libfetch_cache_get(ctx, want, AF_INET) == NULL, "a different user does not match");
    strcpy(want->user, "user");

    strcpy(want->pwd, "wrong");
    test_ok(libfetch_cache_get(ctx, want, AF_INET) == NULL, "a different password does not match");
    strcpy(want->pwd, "pw");

    test_ok(libfetch_cache_get(ctx, want, AF_INET6) == NULL,
            "a different address family does not match");

    /* The exact identity does -- and a is the *older* entry, so the
     * walk has to step past b to find it. */
    got = libfetch_cache_get(ctx, want, AF_INET);
    test_ok(got == a, "the exact identity matches, past a newer entry");
    if (got)
        libfetch_close(got);

    /* AF_UNSPEC on either side is a wildcard. */
    libfetch_free_url(want);
    want = libfetch_parse_url("http://b.example:8080/x");
    got = want ? libfetch_cache_get(ctx, want, AF_INET6) : NULL;
    test_ok(got == b, "a cached AF_UNSPEC connection matches any requested family");
    if (got)
        libfetch_close(got);

    test_ok(want && libfetch_cache_get(ctx, want, AF_UNSPEC) == NULL,
            "a taken connection is out of the cache");

    libfetch_free_url(want);
    libfetch_ctx_free(ctx);
}

static void check_cache_eviction(void)
{
    struct libfetch_ctx *ctx = libfetch_ctx_new(2, 1);
    libfetch_conn_t *a, *b, *c, *d;

    a = fake_conn("http://a.example/", AF_UNSPEC);
    b = fake_conn("http://a.example/", AF_UNSPEC);
    c = fake_conn("http://b.example/", AF_UNSPEC);
    d = fake_conn("http://c.example/", AF_UNSPEC);
    if (!ctx || !a || !b || !c || !d) {
        test_ok(0, "context and connections for the eviction cases");
        return;
    }

    closed_count = 0;
    libfetch_cache_put(ctx, a, counting_close);
    test_int_eq(closed_count, 0, "the first connection is stored");

    /* A second connection to the same host breaks the per-host limit
     * of one: the older entry goes. */
    libfetch_cache_put(ctx, b, counting_close);
    test_int_eq(closed_count, 1, "a same-host duplicate evicts the older entry");

    /* Another host fits beside it... */
    libfetch_cache_put(ctx, c, counting_close);
    test_int_eq(closed_count, 1, "a second host is stored beside the first");

    /* ...but a third breaks the global limit of two, and the entry
     * evicted is the oldest -- in the middle of the walk, not at its
     * head. */
    libfetch_cache_put(ctx, d, counting_close);
    test_int_eq(closed_count, 2, "a third host evicts the oldest entry");

    /* Freeing the context closes whatever survived. */
    libfetch_ctx_free(ctx);
}

int main(void)
{
    check_parseuint();
    check_default_ports();
    check_ctx_limits();
    check_cache_refusals();
    check_cache_matching();
    check_cache_eviction();

    return test_summary();
}
