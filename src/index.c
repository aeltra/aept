/* index.c - the repository metadata stanza at the head of an index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "aept/index.h"
#include "aept/msg.h"
#include "aept/util.h"

#define INDEX_TIMESTAMP_FMT "%Y-%m-%dT%H:%M:%SZ"

int aept_index_header_field(const char *path, const char *field, char *out, size_t out_len)
{
    FILE *fp;
    char buf[512];
    size_t field_len = strlen(field);
    int r = -1;

    fp = fopen(path, "r");
    if (!fp)
        return -1;

    while (fgets(buf, sizeof(buf), fp)) {
        const char *value;
        size_t len;

        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            aept_fgets_drain_line(fp);
            continue;
        }

        /* The header is the first stanza and nothing else, so stop at the
         * blank line that ends it. */
        if (buf[0] == '\n' || buf[0] == '\r' || buf[0] == '\0')
            break;

        /*
         * A Package field anywhere in this stanza means it is a package and
         * the index carries no header -- which is what one published before
         * the header existed looks like.  Checked across the whole stanza
         * rather than on the first line alone: the control format fixes no
         * field order, so a producer other than ours may not lead with it,
         * and reading a package's fields as repository metadata would let a
         * package claim the index never expires.
         */
        if (strncmp(buf, "Package:", 8) == 0) {
            r = -1;
            break;
        }

        if (r == 0 || strncmp(buf, field, field_len) != 0 || buf[field_len] != ':')
            continue;

        value = buf + field_len + 1;
        while (*value == ' ' || *value == '\t')
            value++;

        len = strcspn(value, "\r\n");
        if (len >= out_len)
            len = out_len - 1;

        memcpy(out, value, len);
        out[len] = '\0';
        r = 0;
    }

    fclose(fp);
    return r;
}

/* Format the current time the way an index stamps its Date. */
static int utc_now(char *out, size_t out_len)
{
    time_t now = time(NULL);
    struct tm tm;

    if (!gmtime_r(&now, &tm))
        return -1;

    return strftime(out, out_len, INDEX_TIMESTAMP_FMT, &tm) == 0 ? -1 : 0;
}

int aept_index_check_expiry(const char *path, const char *name, int fatal)
{
    char valid_until[AEPT_INDEX_TIMESTAMP_LEN + 1];
    char now[AEPT_INDEX_TIMESTAMP_LEN + 1];

    if (aept_index_header_field(path, "Valid-Until", valid_until, sizeof(valid_until)) < 0)
        return 0;

    /* No clock is no verdict.  Refusing here would make an unreadable clock
     * indistinguishable from an expired index. */
    if (utc_now(now, sizeof(now)) < 0)
        return 0;

    if (strcmp(now, valid_until) <= 0)
        return 0;

    if (!fatal) {
        aept_log_warning("index for '%s' expired on %s; the repository may be stale", name,
                         valid_until);
        return 0;
    }

    aept_log_error("index for '%s' expired on %s; refusing to use it\n"
                   "  (run 'aept update', or set 'option check_index_expiry 0' to downgrade "
                   "this to a warning)",
                   name, valid_until);
    return -1;
}
