/* validator.c - cache validators kept beside a fetched index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libfetch/fetch.h"

#include "aept/msg.h"
#include "aept/util.h"
#include "aept/validator.h"

/*
 * The record is a control-format stanza, so it reads like everything
 * else aept writes:
 *
 *     URL: https://example.com/testrepo/InPackages.gz
 *     ETag: "5f0a1c-2a4b"
 *     Last-Modified: Wed, 21 Oct 2015 07:28:00 GMT
 *
 * Either validator may be absent; the url never is.  Nothing here is
 * fetched -- it is local state, written after the document it
 * describes has been accepted, and read only to decide whether the
 * next request may be conditional.
 */
#define VALIDATOR_LINE_MAX 4096

/*
 * Everything a server offers as a validator ends up in a request header
 * on the next run, so this is the one place that looks at it.  A header
 * value cannot arrive holding CR or LF -- those delimit the header
 * itself -- but nothing on the wire checks the rest, and the file can
 * also have been edited or truncated since.  Printable ASCII within the
 * length libfetch can hold, or it is not a validator.
 */
static int validator_is_sane(const char *value)
{
    size_t i;

    if (!*value || strlen(value) > LIBFETCH_VALIDATOR_MAX)
        return 0;

    for (i = 0; value[i]; i++) {
        if ((unsigned char)value[i] < 0x20 || (unsigned char)value[i] > 0x7e)
            return 0;
    }

    return 1;
}

/* The value of field in line, or NULL when line carries another field. */
static const char *field_value(const char *line, const char *field)
{
    size_t len = strlen(field);

    if (strncmp(line, field, len) != 0 || line[len] != ':')
        return NULL;

    line += len + 1;
    while (*line == ' ' || *line == '\t')
        line++;

    return line;
}

int aept_validator_load(const char *path, const char *url, struct libfetch_validators *out)
{
    FILE *fp;
    char buf[VALIDATOR_LINE_MAX];
    int url_matched = 0;
    int ok = 1;

    memset(out, 0, sizeof(*out));

    fp = fopen(path, "r");
    if (!fp)
        return -1;

    while (ok && fgets(buf, sizeof(buf), fp)) {
        const char *value;

        /*
         * Reject rather than skip.  A dropped line is not a smaller
         * record here: losing the url line would leave the validators
         * to be sent with any request at all.
         */
        if (aept_fgets_is_truncated(buf, sizeof(buf))) {
            ok = 0;
            break;
        }

        buf[strcspn(buf, "\r\n")] = '\0';
        if (!buf[0])
            continue;

        if ((value = field_value(buf, "URL")) != NULL) {
            url_matched = (strcmp(value, url) == 0);
        } else if ((value = field_value(buf, "ETag")) != NULL) {
            if (validator_is_sane(value))
                snprintf(out->etag, sizeof(out->etag), "%s", value);
            else
                ok = 0;
        } else if ((value = field_value(buf, "Last-Modified")) != NULL) {
            if (validator_is_sane(value))
                snprintf(out->last_modified, sizeof(out->last_modified), "%s", value);
            else
                ok = 0;
        }
    }

    fclose(fp);

    if (!ok || !url_matched || (!out->etag[0] && !out->last_modified[0])) {
        memset(out, 0, sizeof(*out));
        return -1;
    }

    return 0;
}

int aept_validator_save(const char *path, const char *url, const struct libfetch_validators *v)
{
    char *tmp = NULL;
    FILE *fp;
    int r = -1;

    if (!v->etag[0] && !v->last_modified[0]) {
        /* Nothing to record, and an old record must not survive the
         * document it was written for. */
        unlink(path);
        return 0;
    }

    /* Written aside and renamed, as the downloads themselves are: a
     * record half on disk would be read back as malformed on the next
     * run, which is recoverable but needlessly noisy. */
    aept_asprintf(&tmp, "%s.%d", path, (int)getpid());

    fp = fopen(tmp, "w");
    if (!fp) {
        aept_log_debug("cannot write '%s': %s", tmp, strerror(errno));
        free(tmp);
        return -1;
    }

    fprintf(fp, "URL: %s\n", url);
    if (validator_is_sane(v->etag))
        fprintf(fp, "ETag: %s\n", v->etag);
    if (validator_is_sane(v->last_modified))
        fprintf(fp, "Last-Modified: %s\n", v->last_modified);

    if (fclose(fp) != 0) {
        aept_log_debug("cannot write '%s': %s", tmp, strerror(errno));
        goto cleanup;
    }

    if (rename(tmp, path) != 0) {
        aept_log_debug("rename '%s' -> '%s': %s", tmp, path, strerror(errno));
        goto cleanup;
    }

    r = 0;

cleanup:
    if (r != 0)
        unlink(tmp);
    free(tmp);
    return r;
}
