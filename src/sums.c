/* sums.c - a package's shipped sha256sums
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/msg.h"
#include "aept/sums.h"
#include "aept/util.h"

void aept_sums_init(aept_sums_t *s)
{
    memset(s, 0, sizeof(*s));
}

void aept_sums_free(aept_sums_t *s)
{
    int i;

    for (i = 0; i < s->count; i++) {
        free(s->entries[i].path);
        free(s->entries[i].sha256);
    }
    free(s->entries);
    aept_sums_init(s);
}

static const char *strip_leading(const char *p)
{
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    while (p[0] == '/')
        p++;
    return p;
}

static int sum_cmp(const void *a, const void *b)
{
    return strcmp(((const aept_sum_t *)a)->path, ((const aept_sum_t *)b)->path);
}

int aept_sums_load(const char *file, aept_sums_t *s)
{
    FILE *fp;
    char line[4096];
    int lineno = 0, i;

    aept_sums_init(s);

    fp = fopen(file, "r");
    if (!fp)
        return errno == ENOENT ? 1 : -1;

    while (fgets(line, sizeof(line), fp)) {
        const char *path;
        int j;

        lineno++;
        if (aept_fgets_is_truncated(line, sizeof(line))) {
            aept_log_error("sha256sums line %d is too long", lineno);
            goto bad;
        }
        line[strcspn(line, "\n")] = '\0';

        for (j = 0; j < 64; j++)
            if (!isxdigit((unsigned char)line[j]))
                break;
        if (j != 64 || line[64] != ' ' || line[65] != ' ') {
            aept_log_error("sha256sums line %d is not '<sha256>  <path>'", lineno);
            goto bad;
        }
        path = strip_leading(line + 66);
        if (path[0] == '\0') {
            aept_log_error("sha256sums line %d names no file", lineno);
            goto bad;
        }

        if (s->count >= s->alloc) {
            s->alloc = s->alloc ? s->alloc * 2 : 64;
            s->entries = aept_realloc(s->entries, s->alloc * sizeof(*s->entries));
        }
        s->entries[s->count].path = aept_strdup(path);
        s->entries[s->count].sha256 = aept_strdup(line);
        s->entries[s->count].sha256[64] = '\0';
        for (j = 0; j < 64; j++)
            s->entries[s->count].sha256[j] = (char)tolower((unsigned char)line[j]);
        s->count++;
    }

    if (ferror(fp)) {
        aept_log_error("cannot read sha256sums: %s", strerror(errno));
        goto bad;
    }
    fclose(fp);

    if (s->count == 0) {
        aept_log_error("sha256sums lists no files");
        aept_sums_free(s);
        return -1;
    }

    qsort(s->entries, s->count, sizeof(*s->entries), sum_cmp);
    for (i = 1; i < s->count; i++) {
        if (strcmp(s->entries[i - 1].path, s->entries[i].path) == 0) {
            aept_log_error("sha256sums lists '%s' twice", s->entries[i].path);
            aept_sums_free(s);
            return -1;
        }
    }
    return 0;

bad:
    fclose(fp);
    aept_sums_free(s);
    return -1;
}

const char *aept_sums_lookup(const aept_sums_t *s, const char *path)
{
    aept_sum_t key;
    const aept_sum_t *hit;

    if (s->count == 0)
        return NULL;
    key.path = (char *)strip_leading(path);
    hit = bsearch(&key, s->entries, s->count, sizeof(*s->entries), sum_cmp);
    return hit ? hit->sha256 : NULL;
}
