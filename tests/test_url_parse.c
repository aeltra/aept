/* test_url_parse.c - libfetch's URL parser on the edges
 *
 * Every fetch aept makes starts here, with a string from the
 * configuration.  The parser's job on bad input is to refuse -- a URL
 * misread as a different host is a request sent to the wrong place.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "libfetch/fetch.h"
#include "libfetch/common.h"

#include "test.h"

static void check_rejected(const char *url, const char *label)
{
    struct libfetch_url *u = libfetch_parse_url(url);

    test_ok(u == NULL, label);
    if (u)
        libfetch_free_url(u);
}

int main(void)
{
    struct libfetch_url *u;

    /* ── the well-formed baseline ─────────────────────────────────── */

    u = libfetch_parse_url("http://example.com/dir/file");
    test_ok(u != NULL, "a plain http url parses");
    if (u) {
        test_str_eq(u->scheme, "http", "scheme");
        test_str_eq(u->host, "example.com", "host");
        test_int_eq(u->port, 0, "no port means port 0 (resolved later)");
        test_str_eq(u->doc, "/dir/file", "document");
        libfetch_free_url(u);
    }

    u = libfetch_parse_url("https://user:pw@example.com:8443/x");
    test_ok(u != NULL, "credentials and port parse");
    if (u) {
        test_str_eq(u->scheme, "https", "https scheme");
        test_str_eq(u->user, "user", "user");
        test_str_eq(u->pwd, "pw", "password");
        test_int_eq(u->port, 8443, "explicit port");
        libfetch_free_url(u);
    }

    u = libfetch_parse_url("http://example.com");
    test_ok(u != NULL, "a url without a path parses");
    if (u) {
        test_str_eq(u->doc, "/", "and the document defaults to /");
        libfetch_free_url(u);
    }

    u = libfetch_parse_url("http://[::1]:8080/x");
    test_ok(u != NULL, "a bracketed IPv6 host parses");
    if (u) {
        test_str_eq(u->host, "::1", "without its brackets");
        test_int_eq(u->port, 8080, "and with its port");
        libfetch_free_url(u);
    }

    /* Percent-decoding in userinfo, and re-encoding in the document. */
    u = libfetch_parse_url("http://u%40corp:p%3aw@example.com/a b");
    test_ok(u != NULL, "escaped userinfo parses");
    if (u) {
        test_str_eq(u->user, "u@corp", "user decoded");
        test_str_eq(u->pwd, "p:w", "password decoded");
        test_str_eq(u->doc, "/a%20b", "an unsafe document byte is re-encoded");
        libfetch_free_url(u);
    }

    /* ── what must be refused ─────────────────────────────────────── */

    check_rejected("/etc/passwd", "a bare path is not a url");
    check_rejected("file:///etc/passwd", "file: is not a scheme aept has");
    check_rejected("ftp://example.com/x", "ftp is gone from this fork");
    check_rejected("http:example.com", "http without // is malformed");
    check_rejected("http://example.com:99999/", "a port beyond 65535 is refused");
    check_rejected("http://example.com:12ab/", "a non-numeric port is refused");
    check_rejected("http://u%zz:p@example.com/", "an invalid userinfo escape is refused");

    /* ── make / copy round-trip ───────────────────────────────────── */

    u = libfetch_make_url("https", "example.com", 8443, "/doc", "u", "p");
    test_ok(u != NULL, "make_url builds a url");
    if (u) {
        struct libfetch_url *c = libfetch_copy_url(u);

        test_ok(c != NULL, "copy_url copies it");
        if (c) {
            test_str_eq(c->host, "example.com", "the copy keeps the host");
            test_str_eq(c->doc, "/doc", "and the document");
            test_ok(c->doc != u->doc, "with its own document allocation");
            libfetch_free_url(c);
        }
        libfetch_free_url(u);
    }

    test_ok(libfetch_make_url("https", NULL, 0, NULL, "", "") == NULL,
            "make_url with neither host nor doc is refused");
    test_ok(libfetch_make_url("https", "h", -1, "/", "", "") == NULL,
            "make_url with a negative port is refused");

    /* ── small helpers ────────────────────────────────────────────── */

    test_int_eq(libfetch_default_proxy_port("http"), 3128, "the default proxy port is 3128");
    test_int_eq(libfetch_urlpath_safe('a'), 1, "letters are path-safe");
    test_int_eq(libfetch_urlpath_safe(' '), 0, "a space is not");

    return test_summary();
}
