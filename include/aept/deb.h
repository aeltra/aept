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
 * solver never reads: clash.c consults it for permission to overwrite,
 * install.c for the order of a transaction that both installs and
 * removes.
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
 * The repodata key under which Replaces is stored, as a dep array.
 * Read it with solvable_lookup_deparray().
 */
Id aept_deb_replaces_key(Pool *pool);

/*
 * Whether s declares both Replaces and Conflicts for the package named
 * by the id other -- the pair that lets s take that package's place
 * (Policy 7.6.2).  Matched by name: a versioned Replaces bounds which
 * versions may be taken over, and the version in hand is whichever one
 * is installed.
 *
 * Two callers, and they are the two halves of one rule: clash.c allows
 * s to overwrite a path the other package owns, and solver.c orders the
 * install ahead of the removal so the overwrite happens before the
 * remainder goes.
 */
int aept_deb_takes_over(Pool *pool, Solvable *s, Id other);

#endif
