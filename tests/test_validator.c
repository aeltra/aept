/* test_validator.c - the cache validators recorded beside an index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libfetch/fetch.h"

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/util.h"
#include "aept/validator.h"

#include "test.h"

#define URL "https://example.com/testrepo/InPackages.gz"
#define ETAG "\"5f0a1c-2a4b\""
#define LASTMOD "Wed, 21 Oct 2015 07:28:00 GMT"

static struct aept_ctx ctx;
static char path[] = "/tmp/aept-validator-XXXXXX";

/* Failures to write are logged; none is expected, but the context has
 * to exist before aept_log() is called at all. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

/* Whether the record on disk mentions needle anywhere. */
static int file_contains(const char *needle)
{
    FILE *fp = fopen(path, "r");
    char buf[4096];
    size_t n;

    if (!fp) {
        perror(path);
        exit(1);
    }
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    return strstr(buf, needle) != NULL;
}

/* Write a record by hand, to test what the loader accepts. */
static void write_raw(const char *text)
{
    FILE *fp = fopen(path, "w");

    if (!fp) {
        perror(path);
        exit(1);
    }
    fputs(text, fp);
    fclose(fp);
}

static void check_rejected(const char *text, const char *label)
{
    struct libfetch_validators v;

    write_raw(text);
    test_int_eq(aept_validator_load(path, URL, &v), -1, label);
}

int main(void)
{
    struct libfetch_validators saved, loaded;
    char *big;
    int fd;

    silence_logging();

    fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    close(fd);

    /* ── a round trip ─────────────────────────────────────────────── */

    memset(&saved, 0, sizeof(saved));
    snprintf(saved.etag, sizeof(saved.etag), "%s", ETAG);
    snprintf(saved.last_modified, sizeof(saved.last_modified), "%s", LASTMOD);

    test_int_eq(aept_validator_save(path, URL, &saved), 0, "both validators are recorded");
    test_int_eq(aept_validator_load(path, URL, &loaded), 0, "and read back");
    test_str_eq(loaded.etag, ETAG, "the ETag survives verbatim, quotes included");
    test_str_eq(loaded.last_modified, LASTMOD, "so does the date, unparsed");

    /* ── the record belongs to one url ────────────────────────────── *
     *
     * Nothing in a validator says which document it describes, so a
     * record written for InPackages.gz must not be offered up when the
     * very same source is later fetched as Packages.gz.
     */

    test_int_eq(aept_validator_load(path, "https://example.com/testrepo/Packages.gz", &loaded), -1,
                "a record for another url is not used");
    test_str_eq(loaded.etag, "", "and nothing is left in the caller's buffer");

    /* ── credentials never reach the record ───────────────────────── *
     *
     * A source url may carry userinfo.  The record identifies a
     * document, not a login, so what is written and matched is the url
     * with the userinfo stripped -- a state file must not hold a
     * password that was only ever meant for the wire.
     */

    memset(&saved, 0, sizeof(saved));
    snprintf(saved.etag, sizeof(saved.etag), "%s", ETAG);
    test_int_eq(
        aept_validator_save(path, "https://user:secret@example.com/testrepo/InPackages.gz", &saved),
        0, "a record for a credentialed url is written");
    test_int_eq(file_contains("secret"), 0, "without the password in it");
    test_int_eq(file_contains("URL: " URL "\n"), 1, "naming the sanitized url instead");
    test_int_eq(aept_validator_load(path, "https://user:secret@example.com/testrepo/InPackages.gz",
                                    &loaded),
                0, "the credentialed url matches its own record");
    test_int_eq(aept_validator_load(path, URL, &loaded), 0,
                "and so does the same url without the credentials");

    /* ── one validator is enough, none is not ─────────────────────── */

    memset(&saved, 0, sizeof(saved));
    snprintf(saved.last_modified, sizeof(saved.last_modified), "%s", LASTMOD);
    test_int_eq(aept_validator_save(path, URL, &saved), 0, "a date alone is recorded");
    test_int_eq(aept_validator_load(path, URL, &loaded), 0, "and read back");
    test_str_eq(loaded.etag, "", "with no ETag invented for it");
    test_str_eq(loaded.last_modified, LASTMOD, "the date is there");

    memset(&saved, 0, sizeof(saved));
    test_int_eq(aept_validator_save(path, URL, &saved), 0, "saving nothing succeeds");
    test_int_eq(access(path, F_OK), -1, "and removes the record rather than emptying it");
    test_int_eq(aept_validator_load(path, URL, &loaded), -1, "an absent record loads as none");

    /* ── what the loader refuses ──────────────────────────────────── */

    check_rejected("ETag: " ETAG "\n", "a record with no url is refused");
    check_rejected("URL: " URL "\n", "a record with no validator is refused");
    check_rejected("URL: " URL "\nETag: bad\ttab\n", "a control character is refused");

    /*
     * Over-long values are refused rather than truncated at either end.
     * A truncated validator is one that can never match, so it would
     * cost a full download every run while looking like it worked.
     */
    big = aept_malloc(LIBFETCH_VALIDATOR_MAX + 64);
    memset(big, 'x', LIBFETCH_VALIDATOR_MAX + 62);
    big[LIBFETCH_VALIDATOR_MAX + 62] = '\n';
    big[LIBFETCH_VALIDATOR_MAX + 63] = '\0';
    {
        char *text = NULL;

        aept_asprintf(&text, "URL: %s\nETag: %s", URL, big);
        check_rejected(text, "an over-long ETag is refused");
        free(text);
    }
    free(big);

    unlink(path);
    return test_summary();
}
