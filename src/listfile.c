/* listfile.c - the per-package file list, {info_dir}/{name}.list
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "aept/listfile.h"
#include "aept/util.h"

int aept_list_open(aept_list_t *l, const char *path)
{
    memset(l, 0, sizeof(*l));
    l->fp = fopen(path, "r");
    return l->fp ? 0 : -1;
}

void aept_list_close(aept_list_t *l)
{
    if (l->fp)
        fclose(l->fp);
    l->fp = NULL;
}

/* Split l->fields on tabs; returns the column count, columns into cols. */
static int split_columns(char *s, char *cols[], int max)
{
    int n = 0;

    while (n < max) {
        cols[n++] = s;
        s = strchr(s, '\t');
        if (!s)
            break;
        *s++ = '\0';
    }
    return n;
}

int aept_list_next(aept_list_t *l)
{
    aept_list_entry_t *e = &l->entry;

    if (!l->fp)
        return 0;

    for (;;) {
        char *cols[8];
        const char *p;
        int n;

        if (!fgets(l->line, sizeof(l->line), l->fp))
            return 0;

        /* Too long to have been written by aept: dropped whole rather
         * than read in pieces, which would invent paths. */
        if (aept_fgets_is_truncated(l->line, sizeof(l->line))) {
            aept_fgets_drain_line(l->fp);
            continue;
        }

        memcpy(l->fields, l->line, sizeof(l->fields));
        l->fields[strcspn(l->fields, "\n")] = '\0';
        if (l->fields[0] == '\0')
            continue;

        n = split_columns(l->fields, cols, 8);

        memset(e, 0, sizeof(*e));
        e->raw = l->line;
        e->path = cols[0];
        e->uid = e->gid = -1;
        e->size = -1;

        p = e->path;
        while (p[0] == '.' && p[1] == '/')
            p += 2;
        while (p[0] == '/')
            p++;
        e->stripped = p;

        if (n >= 2)
            e->mode = (unsigned int)strtoul(cols[1], NULL, 8);

        if (n == 3) {
            /* The form before uid, gid, size and sha256 were recorded. */
            e->link = cols[2];
        } else if (n >= 6) {
            e->uid = strcmp(cols[2], "-") == 0 ? -1 : strtol(cols[2], NULL, 10);
            e->gid = strcmp(cols[3], "-") == 0 ? -1 : strtol(cols[3], NULL, 10);
            e->size = strcmp(cols[4], "-") == 0 ? -1 : strtoll(cols[4], NULL, 10);
            e->sha256 = strcmp(cols[5], "-") == 0 ? NULL : cols[5];
            if (n >= 7)
                e->link = cols[6];
        }

        return 1;
    }
}

int aept_list_write_line(FILE *fp, const char *path, unsigned int mode, long uid, long gid,
                         long long size, const char *sha256, const char *link)
{
    int r;

    r = fprintf(fp, "%s\t%#03o\t", path, mode);
    if (r > 0)
        r = uid < 0 ? fputs("-\t", fp) : fprintf(fp, "%ld\t", uid);
    if (r >= 0)
        r = gid < 0 ? fputs("-\t", fp) : fprintf(fp, "%ld\t", gid);
    if (r >= 0)
        r = size < 0 ? fputs("-", fp) : fprintf(fp, "%lld", size);
    if (r >= 0)
        r = fprintf(fp, "\t%s", sha256 ? sha256 : "-");
    if (r > 0 && link)
        r = fprintf(fp, "\t%s", link);
    if (r > 0)
        r = fputc('\n', fp) == EOF ? -1 : 1;

    return r > 0 ? 0 : -1;
}
