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

/*
 * Read the next stanza from fp into b, returning 0 at end of input.
 * A blank line ends one; so does end of file, since a .control holds a
 * single stanza and need not end with one.
 */
static int next_stanza(FILE *fp, struct buf *b)
{
    char line[STANZA_LINE_MAX];

    buf_reset(b);

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
            if (b->len)
                return 1;
            continue;
        }

        buf_add(b, line);
    }

    return b->len ? 1 : 0;
}

int aept_stanza_foreach(FILE *fp, int (*cb)(const char *stanza, void *user), void *user)
{
    struct buf b = {NULL, 0, 0};
    int r = 0;

    while (next_stanza(fp, &b)) {
        r = cb(b.p, user);
        if (r)
            break;
    }

    free(b.p);
    return r;
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

/*
 * Read the field beginning at *pos, advancing *pos past it.  Returns 0
 * at the end of the stanza, 1 having filled f.
 *
 * Nothing is copied: f points into the stanza.  A parser reads every
 * field of every stanza and wants only some of them, so copying here
 * would allocate for each one just to have most thrown away -- call
 * aept_stanza_value() for the ones worth keeping.
 */
int aept_stanza_next_field(const char **pos, aept_stanza_field_t *f)
{
    const char *p = *pos;
    const char *eol, *colon, *end;
    size_t llen;

    for (;;) {
        /* Skip what cannot begin a field: a blank line, or an orphan
         * continuation with no field before it. */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            eol = strchr(p, '\n');
            if (!eol) {
                *pos = p + strlen(p);
                return 0;
            }
            p = eol + 1;
        }
        if (!*p) {
            *pos = p;
            return 0;
        }

        eol = strchr(p, '\n');
        llen = eol ? (size_t)(eol - p) : strlen(p);

        colon = memchr(p, ':', llen);
        if (colon)
            break;

        /* Not a field; step over it rather than stopping, so one
         * malformed line does not hide the rest of the stanza. */
        if (!eol) {
            *pos = p + llen;
            return 0;
        }
        p = eol + 1;
    }

    f->name = p;
    f->name_len = (size_t)(colon - p);
    f->value = colon + 1;

    /* The value runs to the end of the last continuation line. */
    end = eol ? eol : p + llen;
    while (eol && (eol[1] == ' ' || eol[1] == '\t')) {
        const char *neol = strchr(eol + 1, '\n');

        end = neol ? neol : eol + 1 + strlen(eol + 1);
        eol = neol;
    }

    f->value_len = (size_t)(end - f->value);
    *pos = eol ? eol + 1 : end;
    return 1;
}

int aept_stanza_field_is(const aept_stanza_field_t *f, const char *name)
{
    return strlen(name) == f->name_len && strncasecmp(f->name, name, f->name_len) == 0;
}

/* Write f's value into out, which must hold value_len + 1 bytes. */
static void copy_value(const aept_stanza_field_t *f, int keep_lines, char *out)
{
    const char *p = f->value, *end = f->value + f->value_len;
    char *w = out, *b;
    int first = 1;

    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *lend = eol ? eol : end;

        if (first) {
            while (p < lend && (*p == ' ' || *p == '\t'))
                p++;
        } else {
            /*
             * The one character that marked the continuation is not
             * part of the value.  Folding drops the rest of the indent
             * too and joins with a single space; keeping the lines
             * leaves it, because in a Description it is the author's.
             */
            if (p < lend)
                p++;
            if (!keep_lines)
                while (p < lend && (*p == ' ' || *p == '\t'))
                    p++;
            *w++ = keep_lines ? '\n' : ' ';
        }

        memcpy(w, p, (size_t)(lend - p));
        w += lend - p;
        first = 0;

        if (!eol)
            break;
        p = eol + 1;
    }

    /* Trim either side: "Depends:  a, b \n" is the same declaration as
     * "Depends: a, b". */
    while (w > out && (w[-1] == ' ' || w[-1] == '\t' || w[-1] == '\r'))
        w--;
    *w = '\0';

    b = out;
    while (*b == ' ' || *b == '\t')
        b++;
    if (b != out)
        memmove(out, b, strlen(b) + 1);
}

char *aept_stanza_value(const aept_stanza_field_t *f, int keep_lines)
{
    /* Folding only ever shortens, so the span is a safe bound. */
    char *out = aept_malloc(f->value_len + 1);

    copy_value(f, keep_lines, out);
    return out;
}

const char *aept_stanza_value_into(const aept_stanza_field_t *f, int keep_lines,
                                   aept_stanza_buf_t *b)
{
    size_t need = f->value_len + 1;

    if (need > b->cap) {
        /* Geometric, so an index of growing stanzas does not realloc
         * for each one. */
        b->cap = b->cap * 2 > need ? b->cap * 2 : need;
        b->p = aept_realloc(b->p, b->cap);
    }

    copy_value(f, keep_lines, b->p);
    return b->p;
}

void aept_stanza_buf_free(aept_stanza_buf_t *b)
{
    free(b->p);
    b->p = NULL;
    b->cap = 0;
}

static char *stanza_field(const char *stanza, const char *field, int keep_lines)
{
    const char *pos = stanza;
    aept_stanza_field_t f;

    while (aept_stanza_next_field(&pos, &f))
        if (aept_stanza_field_is(&f, field))
            return aept_stanza_value(&f, keep_lines);

    return NULL;
}

char *aept_stanza_field(const char *stanza, const char *field)
{
    return stanza_field(stanza, field, 0);
}

char *aept_stanza_field_lines(const char *stanza, const char *field)
{
    return stanza_field(stanza, field, 1);
}
