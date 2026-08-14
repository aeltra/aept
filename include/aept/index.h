/* index.h - the repository metadata stanza at the head of an index
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef INDEX_H_7BF97F
#define INDEX_H_7BF97F

#include <stddef.h>

/*
 * A signed index opens with a stanza carrying repository metadata rather
 * than a package -- Origin, Date and Valid-Until.  Timestamps in it are UTC
 * and fixed width ("YYYY-MM-DDTHH:MM:SSZ"), so comparing two of them is
 * strcmp and needs no date parsing.
 */
#define AEPT_INDEX_TIMESTAMP_LEN 20

/*
 * Read one field out of an index's leading stanza.
 *
 * Returns 0 and fills out on success, -1 if the index carries no header
 * stanza or the field is not in it.
 */
int aept_index_header_field(const char *path, const char *field, char *out, size_t out_len);

/*
 * Check an index against its Valid-Until.
 *
 * Called when the index is loaded rather than when it is fetched, because
 * the client this is meant to catch is one whose updates never arrive: an
 * attacker who drops the request entirely leaves a client sitting on a stale
 * index forever, and no check on the update path can see that.
 *
 * With `fatal` clear an expired index warns and is used anyway; with it set
 * the index is refused.  Which one a deployment wants depends on whether it
 * runs a re-signing job that republishes on a timer -- without one, every
 * archive that stops receiving uploads eventually expires.  See
 * check_index_expiry in the configuration.
 *
 * An index carrying no Valid-Until is never refused: repositories indexed
 * before the field existed have to keep working.
 *
 * Returns 0 if the index may be used, -1 if it must not be.
 */
int aept_index_check_expiry(const char *path, const char *name, int fatal);

#endif /* INDEX_H_7BF97F */
