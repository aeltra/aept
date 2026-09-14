/* clash.h - file clash detection
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef CLASH_H_7BF97F
#define CLASH_H_7BF97F

#include <solv/pool.h>

#include "aept/owner_index.h"
#include "aept/util.h"

struct aept_ctx;

/*
 * Paths taken over from a package that stays installed -- Policy
 * 7.6.1's reading of Replaces, where the file "will no longer be listed
 * as owned by the old package".
 *
 * Collected during the clash check and applied only once the install
 * has actually succeeded: a package disowning a file that then failed
 * to be replaced would leave the path owned by nobody, and a later
 * removal of either package would not clean it up.
 */
typedef struct {
    char *owner; /* the package that is to stop claiming the path */
    char *path;  /* normalized: no leading "./" or "/" */
} aept_takeover_entry_t;

typedef struct {
    aept_takeover_entry_t *entries;
    int count;
    int alloc;
} aept_takeover_list_t;

void aept_takeover_list_init(aept_takeover_list_t *tl);
void aept_takeover_list_free(aept_takeover_list_t *tl);

/* Check for file clashes before extracting a package.
 * old_files: fileset of old version (for upgrades), or NULL.
 * owners: file->owner index covering the current transaction state.
 * taken: receives the 7.6.1 takeovers; required, since granting one
 *   without recording it is what leaves a stale .list behind.
 * Returns the number of clashes (0 = OK), -1 on error. */
int aept_clash_check(struct aept_ctx *ctx, const char *ipk_path, Pool *pool, Id p,
                     aept_fileset_t *old_files, aept_owner_index_t *owners,
                     aept_takeover_list_t *taken);

/*
 * Strike each taken-over path from its former owner's .list, so that
 * removing that package later does not delete a file it no longer
 * owns.  Called as soon as the overwriter's files are on disk -- before
 * its postinst, whose failure changes nothing about who owns them.
 * Reports failures and carries on: the files are already on disk and
 * belong to the new package whatever happens here.
 *
 * An owner left with no files at all has "disappeared" and is handed to
 * aept_do_disappear(), which is why the overwriter is named: its postrm
 * is told who took its files.  One that another installed package
 * depends on is kept instead, owning nothing.
 */
void aept_clash_commit_takeovers(struct aept_ctx *ctx, aept_takeover_list_t *taken, Pool *pool,
                                 Id overwriter, aept_owner_index_t *owners);

#endif
