/* sums.h - a package's shipped sha256sums
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef SUMS_H_7BF97F
#define SUMS_H_7BF97F

/*
 * The "sha256sums" member of a control archive: one line per regular
 * file in the data archive, in sha256sum(1)'s format --
 *
 *   <64 hex digits>  <path>
 *
 * two spaces, the path relative with no leading "./" or "/".  What it
 * asserts is the content the packager built, so aept checks each file
 * it writes against it before the file goes into place, and records
 * that digest rather than its own.  A line that does not parse, a
 * duplicated path, or an empty file makes the whole member invalid,
 * and a package with an invalid one is refused: a digest list that
 * cannot be read is not one to trust half of.
 */
typedef struct {
    char *path;   /* as listed, leading "./" and "/" stripped */
    char *sha256; /* 64 lowercase hex digits */
} aept_sum_t;

typedef struct {
    aept_sum_t *entries; /* sorted by path */
    int count;
    int alloc;
} aept_sums_t;

void aept_sums_init(aept_sums_t *s);
void aept_sums_free(aept_sums_t *s);

/* Read the member at `file`.  0 when read, 1 when there is no such
 * file, -1 when it is malformed (logged). */
int aept_sums_load(const char *file, aept_sums_t *s);

/* The digest listed for a path (leading "./" and "/" ignored), or
 * NULL when the path is not listed. */
const char *aept_sums_lookup(const aept_sums_t *s, const char *path);

#endif
