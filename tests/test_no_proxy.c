/* test_no_proxy.c - $NO_PROXY parsing and matching
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * The first coverage this code has had.  It decides whether a request
 * goes through the configured proxy or straight out, so a wrong answer
 * either sends traffic past a proxy the operator meant to be used, or
 * fails a fetch that should have gone direct.  Neither is loud.
 *
 * `libfetch_no_proxy_match()` is internal, hidden from the shared
 * object by -fvisibility=hidden; this test links the static archive,
 * where that does not apply.
 */

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "libfetch/fetch.h"
#include "libfetch/common.h"

#include "test.h"

/* check <no_proxy value> <host> <expected> */
static void check(const char *value, const char *host, int want, const char *label)
{
    if (value)
        setenv("NO_PROXY", value, 1);
    else
        unsetenv("NO_PROXY");
    unsetenv("no_proxy");

    test_int_eq(libfetch_no_proxy_match(host), want, label);
}

int main(void)
{
    /* Nothing configured: everything goes through the proxy. */
    check(NULL, "example.com", 0, "unset: no host is bypassed");
    check("", "example.com", 0, "empty: no host is bypassed");

    /* The wildcard, kept for compatibility with curl and lynx. */
    check("*", "example.com", 1, "*: every host is bypassed");
    check("*", "10.0.0.1", 1, "*: literal addresses too");

    /* A domain covers itself and everything under it. */
    check("example.com", "example.com", 1, "the domain itself matches");
    check("example.com", "repo.example.com", 1, "a subdomain matches");
    check("example.com", "a.b.example.com", 1, "a deeper subdomain matches");
    check("com", "example.com", 1, "a bare TLD matches what is under it");

    /*
     * ...and nothing else.  The match used to be a bare suffix compare,
     * so "example.com" also matched "notexample.com" and the request
     * quietly went direct instead of through the proxy.
     */
    check("example.com", "notexample.com", 0,
          "a suffix that is not a label boundary does not match");
    check("ample.com", "example.com", 0, "a partial label does not match");
    check("example.com", "example.com.evil.net", 0, "the domain in the middle does not match");
    check("example.com", "example.org", 0, "a different domain does not match");
    check("example.com", "com", 0, "a host shorter than the entry does not match");

    /* A leading dot is accepted and means the same thing, as in curl. */
    check(".example.com", "repo.example.com", 1, "a leading dot still matches subdomains");
    check(".example.com", "example.com", 1, "a leading dot still matches the domain");
    check(".example.com", "notexample.com", 0, "a leading dot does not reintroduce the suffix bug");

    /* Case folding. */
    check("EXAMPLE.COM", "repo.example.com", 1, "the entry is matched case-insensitively");
    check("example.com", "REPO.EXAMPLE.COM", 1, "the host is matched case-insensitively");

    /* Separators: commas, spaces, or both. */
    check("a.com,example.com", "repo.example.com", 1, "comma-separated entries");
    check("a.com, example.com", "repo.example.com", 1, "comma and space");
    check("a.com example.com", "repo.example.com", 1, "space-separated entries");
    check("a.com,b.com", "c.com", 0, "no entry matches");

    /* CIDR blocks, for literal addresses. */
    check("10.0.0.0/8", "10.1.2.3", 1, "an address inside an IPv4 block");
    check("10.0.0.0/8", "11.1.2.3", 0, "an address outside an IPv4 block");
    check("10.0.0.0/8", "example.com", 0, "a name is not matched against a block");
    check("2001:db8::/32", "2001:db8::1", 1, "an address inside an IPv6 block");
    check("2001:db8::/32", "2001:db9::1", 0, "an address outside an IPv6 block");
    check("10.0.0.0/0", "11.1.2.3", 0, "a zero prefix length is rejected");
    check("10.0.0.0/999", "10.1.2.3", 0, "an absurd prefix length is rejected");
    check("10.0.0.0/-1", "10.1.2.3", 0, "a negative prefix length is rejected");

    /* The lowercase spelling is honoured when the uppercase one is not
     * set -- and only then. */
    unsetenv("NO_PROXY");
    setenv("no_proxy", "example.com", 1);
    test_int_eq(libfetch_no_proxy_match("repo.example.com"), 1, "lowercase no_proxy is honoured");
    unsetenv("no_proxy");

    return test_summary();
}
