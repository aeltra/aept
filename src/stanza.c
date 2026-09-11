/* stanza.c - reading a field back out of a control stanza
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/stanza.h"
#include "aept/util.h"

/* Long enough for a Depends line on a package with many dependencies;
 * anything longer is refused rather than read in pieces, as everywhere
 * else in the tree that reads a line. */
#define STANZA_LINE_MAX 8192

/* A stanza accumulated as it is read. */
struct buf {
    char *p;
    size_t len, cap;
};

static void buf_add(struct buf *b, const char *s)
{
    size_t n = strlen(s);

    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap)
            b->cap = b->cap ? b->cap * 2 : 1024;
        b->p = aept_realloc(b->p, b->cap);
    }
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

static void buf_reset(struct buf *b)
{
    b->len = 0;
    if (b->p)
        b->p[0] = '\0';
}

/*
 * Does this stanza name the package and version wanted?  Read from the
 * accumulated text rather than tracked while reading, so the two fields
 * may appear in either order, as the format allows.
 */
static int stanza_matches(const char *stanza, const char *name, const char *version)
{
    char *got;
    int ok;

    got = aept_stanza_field(stanza, "Package");
    ok = got && strcmp(got, name) == 0;
    free(got);
    if (!ok)
        return 0;

    if (!version)
        return 1;

    got = aept_stanza_field(stanza, "Version");
    ok = got && strcmp(got, version) == 0;
    free(got);
    return ok;
}

char *aept_stanza_find(const char *path, const char *name, const char *version)
{
    FILE *fp;
    char line[STANZA_LINE_MAX];
    struct buf b = {NULL, 0, 0};
    char *found = NULL;

    fp = fopen(path, "r");
    if (!fp)
        return NULL;

    while (fgets(line, sizeof(line), fp)) {
        /*
         * An over-long line is dropped whole.  Reading its tail as a
         * line of its own would invent a field, and a stanza is only
         * ever used here to answer a question about itself.
         */
        if (aept_fgets_is_truncated(line, sizeof(line))) {
            aept_fgets_drain_line(fp);
            continue;
        }

        if (line[0] == '\n' || line[0] == '\r') {
            if (b.len && stanza_matches(b.p, name, version)) {
                found = b.p;
                b.p = NULL;
                break;
            }
            buf_reset(&b);
            continue;
        }

        buf_add(&b, line);
    }

    /* A file holding one stanza need not end with a blank line, and a
     * .control file generally does not. */
    if (!found && b.len && stanza_matches(b.p, name, version)) {
        found = b.p;
        b.p = NULL;
    }

    fclose(fp);
    free(b.p);
    return found;
}

char *aept_stanza_field(const char *stanza, const char *field)
{
    size_t flen = strlen(field);
    const char *p = stanza;

    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t llen = eol ? (size_t)(eol - p) : strlen(p);

        /* A continuation belongs to the previous field, never starts
         * one, so it cannot be mistaken for a field of its own. */
        if (llen > flen && (p[0] != ' ' && p[0] != '\t') && strncasecmp(p, field, flen) == 0 &&
            p[flen] == ':') {
            struct buf v = {NULL, 0, 0};
            const char *q = p + flen + 1;
            size_t vlen = llen - flen - 1;
            char *tmp;

            /* The value, then any continuation lines folded onto it. */
            tmp = aept_malloc(vlen + 1);
            memcpy(tmp, q, vlen);
            tmp[vlen] = '\0';
            buf_add(&v, tmp);
            free(tmp);

            while (eol) {
                const char *next = eol + 1;
                const char *neol, *val;
                size_t nlen;

                if (*next != ' ' && *next != '\t')
                    break;
                neol = strchr(next, '\n');

                /* The indent that marks a continuation is not part of
                 * the value; exactly one space joins it to what came
                 * before, however deeply it was indented. */
                val = next;
                while (*val == ' ' || *val == '\t')
                    val++;
                if (neol && val > neol)
                    val = neol;
                nlen = neol ? (size_t)(neol - val) : strlen(val);

                tmp = aept_malloc(nlen + 2);
                tmp[0] = ' ';
                memcpy(tmp + 1, val, nlen);
                tmp[nlen + 1] = '\0';
                buf_add(&v, tmp);
                free(tmp);
                eol = neol;
            }

            /* Trim the space either side: "Depends:  a, b \n" is the
             * same declaration as "Depends: a, b". */
            {
                char *s = v.p;
                char *e;

                while (*s == ' ' || *s == '\t')
                    s++;
                e = s + strlen(s);
                while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
                    e--;
                *e = '\0';
                tmp = aept_strdup(s);
            }
            free(v.p);
            return tmp;
        }

        if (!eol)
            break;
        p = eol + 1;
    }

    return NULL;
}
