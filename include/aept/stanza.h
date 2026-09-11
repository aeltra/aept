/* stanza.h - reading a field back out of a control stanza
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef STANZA_H_7BF97F
#define STANZA_H_7BF97F

/*
 * What a package declared, as opposed to what the solver made of it.
 *
 * libsolv's pool is a solving representation: it renders dependencies
 * its own way, appends every package's implicit self-provide, folds
 * Debian's Replaces into obsoletes, and scales Installed-Size into
 * bytes.  All of that is right for solving and wrong for "aept show",
 * which promises the fields the packager wrote.  So display reads the
 * stanza the pool was built from.
 *
 * Only "show" uses this: one package, one stanza, once.  It is a linear
 * scan of the index, which would be the wrong shape for anything that
 * walks every solvable -- "list" must keep asking the pool.
 */

/*
 * The stanza for name/version, as text, or NULL when the file has no
 * such stanza or cannot be read.  Both must match: an index holds many
 * versions of a package.  The caller frees the result.
 */
char *aept_stanza_find(const char *path, const char *name, const char *version);

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

#endif
