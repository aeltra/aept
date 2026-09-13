/* stanza.h - reading a field back out of a control stanza
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef STANZA_H_7BF97F
#define STANZA_H_7BF97F

#include <stddef.h>
#include <stdio.h>

/*
 * Reading fields back out of a control stanza.
 *
 * Two callers, for different reasons.  deb.c parses an index with these
 * to build solvables, so this is where the format is actually read.
 * "aept show" uses them again on a single stanza, because the pool is a
 * solving representation and not a record of what the packager wrote:
 * it renders "libx (>= 1.0)" its own way, appends every package's
 * implicit self-provide, and scales Installed-Size into bytes.
 *
 * Finding one package's stanza is a linear scan of the index, which is
 * the right shape for "show" -- one package, once -- and the wrong one
 * for anything walking every solvable.  "list" must keep asking the
 * pool.
 */

/*
 * The stanza for name/version, as text, or NULL when the file has no
 * such stanza or cannot be read.  Both must match: an index holds many
 * versions of a package.  A NULL version matches whichever version
 * comes first.  The caller frees the result.
 */
char *aept_stanza_find(const char *path, const char *name, const char *version);

/*
 * Call cb for each stanza in fp, from the current position, until cb
 * returns non-zero or the input ends; that value is returned.  The
 * stanza text is valid only for the duration of the call.
 */
int aept_stanza_foreach(FILE *fp, int (*cb)(const char *stanza, void *user), void *user);

/*
 * The value of one field, or NULL when the stanza does not carry it.
 * The name is matched case-insensitively, as control fields are.
 *
 * Continuation lines are folded onto one, separated by a single space:
 * right for the relationship fields this serves, and not for
 * Description, whose lines are meant to stay apart -- that one still
 * comes from the pool.  The caller frees the result.
 */
char *aept_stanza_field(const char *stanza, const char *field);

/*
 * The same, with the value's lines kept apart: continuations are joined
 * with a newline and lose only the one character that marked them.
 * For Description, whose first line is the summary and whose body is
 * meant to stay laid out as written.  The caller frees the result.
 */
char *aept_stanza_field_lines(const char *stanza, const char *field);

/*
 * A field as it sits in the stanza: pointers into it, nothing copied.
 * Neither string is NUL-terminated.
 */
typedef struct {
    const char *name;
    size_t name_len;
    const char *value; /* raw; spans the field's continuation lines */
    size_t value_len;
} aept_stanza_field_t;

/*
 * Walk a stanza one field at a time: *pos is where to read from and is
 * advanced past the field read.  Returns 0 at the end of the stanza, 1
 * having filled f.
 *
 * This is the shape a parser wants.  The two lookups above each restart
 * at the top of the stanza and copy what they find, which is right for
 * "show" asking after one field and wrong for deb.c, which reads every
 * field of every stanza and keeps only some.
 */
int aept_stanza_next_field(const char **pos, aept_stanza_field_t *f);

/* Whether f is the named field, matched case-insensitively. */
int aept_stanza_field_is(const aept_stanza_field_t *f, const char *name);

/*
 * Copy out f's value -- the only place this allocates.  With keep_lines
 * the continuation lines stay apart, as aept_stanza_field_lines()
 * returns them; without it they fold onto one line separated by single
 * spaces.  The caller frees the result.
 */
char *aept_stanza_value(const aept_stanza_field_t *f, int keep_lines);

/*
 * A buffer reused across calls.  Reading every field of every stanza in
 * an index is the common case and allocating per field is what made it
 * expensive -- against musl, whose allocator costs about nine times
 * glibc's for blocks this small, it dominated the parse.  Zero-initialize
 * it, pass it to as many fields as you like, and free it once.
 */
typedef struct {
    char *p;
    size_t cap;
} aept_stanza_buf_t;

/*
 * f's value in b, valid until the next call against the same buffer.
 * For a value used and finished with before the next field is read; one
 * that must outlive it has to be copied out.
 */
const char *aept_stanza_value_into(const aept_stanza_field_t *f, int keep_lines,
                                   aept_stanza_buf_t *b);

void aept_stanza_buf_free(aept_stanza_buf_t *b);

#endif
