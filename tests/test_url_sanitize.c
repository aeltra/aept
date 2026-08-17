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

static void check_split(const char *in, const char *want_clean, const char *want_user,
                        const char *want_password, const char *label)
{
    char *clean = NULL, *user = NULL, *password = NULL;

    test_int_eq(aept_url_split(in, &clean, &user, &password), 0, label);
    test_str_eq(clean, want_clean, "  ...the clean url");
    test_str_eq(user, want_user, "  ...the user");
    test_str_eq(password, want_password, "  ...the password");
    free(clean);
    free(user);
    free(password);
}

static void check_split_rejected(const char *in, const char *label)
{
    char *clean, *user, *password;

    test_int_eq(aept_url_split(in, &clean, &user, &password), -1, label);
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

    /*
     * The split keeps the credentials apart from the string.  Decoding
     * must mirror libfetch's parser exactly -- these escapes used to be
     * decoded there, so a difference here would change what a working
     * configuration sends.
     */

    check_split("https://user:secret@example.com/x", "https://example.com/x", "user", "secret",
                "credentials are split off");
    check_split("https://user@example.com/x", "https://example.com/x", "user", NULL,
                "a user alone has no password, not an empty one");
    check_split("https://example.com/x", "https://example.com/x", NULL, NULL,
                "no userinfo, no credentials");
    check_split("https://u%40corp:p%3Aw%25@example.com/", "https://example.com/", "u@corp", "p:w%",
                "percent-escapes are decoded, as libfetch decoded them");
    check_split("https://:pw@example.com/", "https://example.com/", "", "pw",
                "an empty user is kept, so the password still pairs with it");
    check_split("http://u:p@ss@example.com/x", "http://example.com/x", "u", "p@ss",
                "a raw '@' in the password reads as the user meant it");

    check_split_rejected("https://u%zz:p@example.com/", "an invalid escape is refused");
    check_split_rejected("https://u%00:p@example.com/", "%00 is refused, as libfetch refused it");
    check_split_rejected("https://u%:p@example.com/", "a truncated escape is refused");

    return test_summary();
}
