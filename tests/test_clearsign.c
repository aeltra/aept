/* test_clearsign.c - splitting signify clearsigned documents
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/clearsign.h"
#include "aept/msg.h"
#include "aept/util.h"

#include "test.h"

#define MSG_BEGIN "-----BEGIN SIGNIFY SIGNED MESSAGE-----\n"
#define SIG_BEGIN "-----BEGIN SIGNIFY SIGNATURE-----\n"
#define SIG_END "-----END SIGNIFY SIGNATURE-----\n"

/* A representative usign signature: comment line plus base64 payload. */
#define SIGNATURE                                                                                  \
    "untrusted comment: signed by key abcdef0123456789\n"                                          \
    "RWSabcdef0123456789ABCDEFabcdefghijklmnopqrstuvwxyz0123456789==\n"

static struct aept_ctx ctx;

/* Malformed documents are expected here and each logs an error. */
static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

/* Compare a parsed region against an expected NUL-terminated string. */
static void check_region(const char *got, size_t got_len, const char *want, const char *label)
{
    int pass = (got_len == strlen(want)) && memcmp(got, want, got_len) == 0;

    test_ok(pass, label);

    if (!pass)
        printf("#   got  (%zu bytes): %.*s\n#   want (%zu bytes): %s\n", got_len, (int)got_len, got,
               strlen(want), want);
}

static void check_malformed(const char *doc, const char *label)
{
    aept_clearsign_t cs;
    test_int_eq(aept_clearsign_parse(doc, strlen(doc), &cs), -1, label);
}

int main(void)
{
    aept_clearsign_t cs;

    silence_logging();

    /* ── the ordinary case ───────────────────────────────────────── */
    {
        static const char doc[] =
            MSG_BEGIN "Package: foo\nVersion: 1.0\n"
                      "\n"
                      "Package: bar\nVersion: 2.0\n" SIG_BEGIN SIGNATURE SIG_END;

        test_int_eq(aept_clearsign_parse(doc, sizeof(doc) - 1, &cs), 0,
                    "well-formed document parses");
        check_region(cs.msg, cs.msg_len,
                     "Package: foo\nVersion: 1.0\n\nPackage: bar\nVersion: 2.0\n",
                     "message is byte-exact, trailing newline included");
        check_region(cs.sig, cs.sig_len, SIGNATURE, "signature is byte-exact");
    }

    /*
     * ── a marker forged inside the index ────────────────────────
     *
     * The format has no escaping, so a package field could carry a line
     * identical to the signature marker.  Splitting at the first match
     * would truncate the index; the real signature block is always the
     * last one.
     */
    {
        static const char doc[] =
            MSG_BEGIN "Package: evil\nVersion: 1.0\nDescription: nasty\n" SIG_BEGIN
                      "not the real signature\n" SIG_END "\n"
                      "Package: good\nVersion: 1.0\n" SIG_BEGIN SIGNATURE SIG_END;

        test_int_eq(aept_clearsign_parse(doc, sizeof(doc) - 1, &cs), 0,
                    "document with an injected marker parses");
        check_region(cs.msg, cs.msg_len,
                     "Package: evil\nVersion: 1.0\nDescription: nasty\n" SIG_BEGIN
                     "not the real signature\n" SIG_END "\n"
                     "Package: good\nVersion: 1.0\n",
                     "split takes the last marker, injected one stays in body");
        check_region(cs.sig, cs.sig_len, SIGNATURE, "real signature recovered despite injection");
    }

    /* ── a marker that does not begin a line is not a marker ─────── */
    {
        static const char doc[] = MSG_BEGIN "Package: foo\nDescription: see x" SIG_BEGIN
                                            "Package: bar\n" SIG_BEGIN SIGNATURE SIG_END;

        test_int_eq(aept_clearsign_parse(doc, sizeof(doc) - 1, &cs), 0,
                    "document with a mid-line marker parses");
        check_region(cs.msg, cs.msg_len,
                     "Package: foo\nDescription: see x" SIG_BEGIN "Package: bar\n",
                     "mid-line marker is not treated as a delimiter");
    }

    /* ── an empty index is still a valid document ────────────────── */
    {
        static const char doc[] = MSG_BEGIN SIG_BEGIN SIGNATURE SIG_END;

        test_int_eq(aept_clearsign_parse(doc, sizeof(doc) - 1, &cs), 0, "empty message parses");
        test_int_eq((int)cs.msg_len, 0, "empty message has zero length");
        check_region(cs.sig, cs.sig_len, SIGNATURE,
                     "signature recovered from empty-message document");
    }

    /* ── trailing bytes after the closing marker are ignored ─────── */
    {
        static const char doc[] = MSG_BEGIN "Package: foo\n" SIG_BEGIN SIGNATURE SIG_END "junk\n";

        test_int_eq(aept_clearsign_parse(doc, sizeof(doc) - 1, &cs), 0,
                    "trailing bytes after the closing marker parse");
        check_region(cs.msg, cs.msg_len, "Package: foo\n",
                     "trailing bytes do not disturb the message");
        check_region(cs.sig, cs.sig_len, SIGNATURE, "trailing bytes do not disturb the signature");
    }

    /* ── malformed documents are rejected ────────────────────────── */

    check_malformed("", "empty input rejected");
    check_malformed("Package: foo\nVersion: 1.0\n", "plain index without a header rejected");
    check_malformed("-----BEGIN SIGNIFY SIGNED MESSAGE-----", "truncated header rejected");
    check_malformed(MSG_BEGIN "Package: foo\n", "document with no signature block rejected");
    check_malformed(MSG_BEGIN "Package: foo\n" SIG_BEGIN SIGNATURE,
                    "unterminated signature block rejected");
    check_malformed(" " MSG_BEGIN "Package: foo\n" SIG_BEGIN SIGNATURE SIG_END,
                    "header not at the start rejected");

    /* ── writing the two regions out ─────────────────────────────── */
    {
        static const char doc[] =
            MSG_BEGIN "Package: foo\nVersion: 1.0\n" SIG_BEGIN SIGNATURE SIG_END;
        char dir_template[] = "/tmp/aept-test-clearsign-XXXXXX";
        char *dir = mkdtemp(dir_template);
        char *msg_path = NULL, *sig_path = NULL;
        char readback[512];
        FILE *fp;
        size_t n;

        if (!dir) {
            perror("mkdtemp");
            return 2;
        }

        aept_asprintf(&msg_path, "%s/Packages", dir);
        aept_asprintf(&sig_path, "%s/Packages.sig", dir);

        test_int_eq(aept_clearsign_parse(doc, sizeof(doc) - 1, &cs), 0,
                    "document for write test parses");
        test_int_eq(aept_clearsign_write(&cs, msg_path, sig_path), 0, "regions are written out");

        fp = fopen(msg_path, "rb");
        n = fp ? fread(readback, 1, sizeof(readback), fp) : 0;
        if (fp)
            fclose(fp);
        check_region(readback, n, "Package: foo\nVersion: 1.0\n",
                     "message file matches the parsed region");

        fp = fopen(sig_path, "rb");
        n = fp ? fread(readback, 1, sizeof(readback), fp) : 0;
        if (fp)
            fclose(fp);
        check_region(readback, n, SIGNATURE, "signature file matches the parsed region");

        unlink(msg_path);
        unlink(sig_path);
        rmdir(dir);
        free(msg_path);
        free(sig_path);
    }

    return test_summary();
}
