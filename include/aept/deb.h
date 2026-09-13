/* deb.h - control stanzas into libsolv solvables
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#ifndef DEB_H_7BF97F
#define DEB_H_7BF97F

#include <stdio.h>

#include <solv/pool.h>
#include <solv/repo.h>

/*
 * aept builds solvables itself rather than calling
 * repo_add_debpackages(), because that reader files Replaces under
 * SOLVABLE_OBSOLETES and obsoletes is not what Replaces means.
 *
 * To libsolv an obsoletes is a supersede: the obsoleting package
 * becomes an update candidate for the obsoleted one, so a plain
 * "upgrade" swaps them.  No Debian field says that.  "Conflicts: X"
 * with "Replaces: X" says X must go for this package to be installed,
 * and that this package may overwrite X's files on the way -- how a
 * removal is carried out, not a reason to start one.  Read as
 * obsoletes, every mail transport agent is an upgrade path for every
 * other.
 *
 * Replaces is therefore kept under a key of aept's own, which the
 * solver never reads.  So is Conflicts, for a different reason: Breaks
 * is folded into the solvable's conflict array too, because to a
 * solver the two are the same thing, and after that the array can no
 * longer say which names the packager wrote under which field.  Policy
 * 7.6 turns on exactly that distinction -- see aept_deb_takeover().
 *
 * The grammar accepted is the binary package one: "name",
 * "name (>= 1.0)", alternatives joined by "|", entries separated by
 * ",".  Architecture lists, build profiles and multiarch qualifiers
 * belong to source packages and are not accepted.  Bare "<" and ">"
 * mean "<=" and ">=" as Policy 7.1 defines them.
 *
 * Version comparison remains libsolv's: only the text is parsed here.
 */

/*
 * Parse an index into repo, one solvable per stanza.  Returns 0, or -1
 * if nothing could be read.  A stanza carrying no Package field is
 * skipped, which is what discards the Origin/Valid-Until header an
 * index opens with; one that cannot be parsed is reported and dropped.
 */
int aept_deb_add_packages(Repo *repo, FILE *fp);

/*
 * One control stanza as a solvable, or 0 if it names no package or
 * cannot be parsed.  Used for a package named on the command line,
 * whose control comes out of the archive rather than an index.
 */
Id aept_deb_add_control(Repo *repo, const char *control);

/*
 * The repodata keys under which Replaces and Conflicts are stored, as
 * dep arrays.  Read them with solvable_lookup_deparray().
 */
Id aept_deb_replaces_key(Pool *pool);
Id aept_deb_conflicts_key(Pool *pool);

/*
 * What Replaces means when s would overwrite a file owned by other.
 *
 * Policy 7.6 gives the field two purposes and makes them disjoint, on
 * whether the two packages conflict:
 *
 *   7.6.2, the pair -- "only takes effect when the two packages *do*
 *   conflict".  other is being removed for s to be installed, and s may
 *   take its files on the way.  The replaced package may be *virtual*
 *   here; Policy's own example is every MTA declaring Provides,
 *   Conflicts and Replaces on mail-transport-agent.
 *
 *   7.6.1, Replaces alone -- "only takes effect when both packages are
 *   at least partially on the system at once.  It is not relevant if
 *   the packages conflict".  Both stay installed and the field decides
 *   only who owns the overlapping files; the path stops being listed as
 *   other's.  Virtual names do *not* count here, by the same section:
 *   the replaced package must be named for real.
 *
 * Breaks is not Conflicts for this purpose (Policy 7.3 against 7.4),
 * which is why the Conflicts key exists apart from s->conflicts: the
 * documented 7.6.1 idiom is Breaks with Replaces on a package split,
 * and reading that as a conflict would route it to 7.6.2 and remove a
 * package Debian keeps.
 */
typedef enum {
    AEPT_TAKEOVER_NONE = 0,  /* not permitted; a clash */
    AEPT_TAKEOVER_OVERWRITE, /* 7.6.1: other stays, and must disown the path */
    AEPT_TAKEOVER_SUPERSEDE, /* 7.6.2: other is going away anyway */
} aept_takeover_mode_t;

aept_takeover_mode_t aept_deb_takeover(Pool *pool, Solvable *s, Solvable *other);

#endif
