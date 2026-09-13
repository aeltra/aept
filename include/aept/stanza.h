/* stanza.h - reading a field back out of a control stanza
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef STANZA_H_7BF97F
#define STANZA_H_7BF97F

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
 * versions of a package.  The caller frees the result.
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

#endif
