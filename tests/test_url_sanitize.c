/* test_url_sanitize.c - userinfo is stripped from anything shown or stored
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdlib.h>

#include "aept/util.h"

#include "test.h"

static void check(const char *in, const char *want, const char *label)
{
    char *out = aept_url_sanitized(in);

    test_str_eq(out, want, label);
    free(out);
}

int main(void)
{
    check("https://user:secret@example.com/x/y.gz", "https://example.com/x/y.gz",
          "user and password are removed");
    check("https://user@example.com/x", "https://example.com/x", "a user alone is removed");
    check("http://u:p@ss@example.com/x", "http://example.com/x",
          "the split is at the last '@' of the authority");
    check("https://example.com/InPackages.gz", "https://example.com/InPackages.gz",
          "a url without userinfo is unchanged");
    check("https://example.com/x@y", "https://example.com/x@y",
          "an '@' in the path is not userinfo");
    check("https://example.com/x?user=a@b", "https://example.com/x?user=a@b",
          "an '@' in the query is not userinfo");
    check("https://u:p@example.com", "https://example.com",
          "an authority with no path still loses its userinfo");
    check("https://u:p@example.com:8080/x", "https://example.com:8080/x",
          "the port survives the stripping");
    check("Packages", "Packages", "a bare name has no scheme and passes through");
    check("", "", "an empty string passes through");

    return test_summary();
}
