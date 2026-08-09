/* clearsign.c - signify clearsigned document splitting
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aept/clearsign.h"
#include "aept/msg.h"

/*
 * The repository indexer wraps the package index like this:
 *
 *   -----BEGIN SIGNIFY SIGNED MESSAGE-----
 *   <index text>-----BEGIN SIGNIFY SIGNATURE-----
 *   <signature>-----END SIGNIFY SIGNATURE-----
 *
 * The index text always ends in a newline, so each marker starts a line
 * of its own.  The signature covers the message bytes exactly as they
 * appear between the header's newline and the first byte of the
 * signature marker, so the split has to be byte-exact — including the
 * message's trailing newline.
 */
#define MSG_BEGIN "-----BEGIN SIGNIFY SIGNED MESSAGE-----\n"
#define SIG_BEGIN "-----BEGIN SIGNIFY SIGNATURE-----\n"
#define SIG_END   "-----END SIGNIFY SIGNATURE-----\n"

/*
 * Find the last line-aligned occurrence of a marker.
 *
 * Searching backwards matters: the format has no escaping, so a package
 * field could in principle contain a line identical to a marker.  The
 * real signature block is always the last one, so taking the last match
 * makes such content harmless instead of truncating the index.
 */
static const char *find_last_marker(const char *start, size_t len,
                                    const char *marker)
{
    size_t mlen = strlen(marker);
    const char *found = NULL;
    const char *p = start;
    size_t remaining = len;

    while (remaining >= mlen) {
        const char *hit = memmem(p, remaining, marker, mlen);
        if (!hit)
            break;

        /* A marker only counts when it begins a line. */
        if (hit == start || hit[-1] == '\n')
            found = hit;

        remaining -= (size_t)(hit - p) + 1;
        p = hit + 1;
    }

    return found;
}

int aept_clearsign_parse(const char *buf, size_t len, aept_clearsign_t *out)
{
    const size_t hdr_len = sizeof(MSG_BEGIN) - 1;
    const char *end = buf + len;
    const char *msg, *sig_begin, *sig, *sig_end;

    if (len < hdr_len || memcmp(buf, MSG_BEGIN, hdr_len) != 0) {
        aept_log_error("signed index is missing its signify header");
        return -1;
    }

    msg = buf + hdr_len;

    sig_begin = find_last_marker(msg, (size_t)(end - msg), SIG_BEGIN);
    if (!sig_begin) {
        aept_log_error("signed index contains no signature block");
        return -1;
    }

    sig = sig_begin + sizeof(SIG_BEGIN) - 1;

    sig_end = find_last_marker(sig, (size_t)(end - sig), SIG_END);
    if (!sig_end) {
        aept_log_error("signed index has an unterminated signature block");
        return -1;
    }

    out->msg     = msg;
    out->msg_len = (size_t)(sig_begin - msg);
    out->sig     = sig;
    out->sig_len = (size_t)(sig_end - sig);

    return 0;
}

static int write_region(const char *path, const char *data, size_t len)
{
    FILE *fp = fopen(path, "wb");

    if (!fp) {
        aept_log_error("cannot write '%s'", path);
        return -1;
    }

    if (len && fwrite(data, 1, len, fp) != len) {
        aept_log_error("failed to write '%s'", path);
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0) {
        aept_log_error("failed to write '%s'", path);
        return -1;
    }

    return 0;
}

int aept_clearsign_write(const aept_clearsign_t *cs, const char *msg_path,
                         const char *sig_path)
{
    if (write_region(msg_path, cs->msg, cs->msg_len) < 0)
        return -1;

    if (write_region(sig_path, cs->sig, cs->sig_len) < 0) {
        unlink(msg_path);
        return -1;
    }

    return 0;
}
