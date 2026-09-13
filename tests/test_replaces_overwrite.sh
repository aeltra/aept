#!/bin/sh
# test_replaces_overwrite.sh - Replaces without Conflicts (Policy 7.6.1).
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Policy gives Replaces two purposes and makes them disjoint on whether
# the two packages conflict.  7.6.2, the pair, is test_supersede.sh and
# test_takeover_order.sh.  This is the other one:
#
#   "if the overwriting package declares that it Replaces the one
#    containing the file being overwritten, then dpkg will replace the
#    file from the old package with that from the new.  The file will no
#    longer be listed as "owned" by the old package and will be taken
#    over by the new package."
#
# Both packages stay installed.  The half that is easy to miss is the
# second sentence: the old package has to stop claiming the path, or
# removing it later deletes a file that is not its any more.  That is
# what the last section here checks, and it is the reason aept refused
# this case until the file list could be rewritten.
#
# Policy also says the documented shape is Breaks with Replaces, for a
# file moving between packages on a split.  Breaks is not Conflicts
# (7.3 against 7.4), so that pair belongs here and not in 7.6.2.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
cache=$root/var/cache/aept
mkdir -p "$cache"

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }
owns() { grep -q "$2" "$root/var/lib/aept/info/$1.list" 2>/dev/null; }

# old ships two files; new takes one of them and brings its own.
mkdir -p "$work/old/usr/bin" "$work/old/usr/share/old"
printf 'old\n' > "$work/old/usr/bin/moved"
printf 'kept\n' > "$work/old/usr/share/old/kept"

mkdir -p "$work/new/usr/bin"
printf 'new\n' > "$work/new/usr/bin/moved"

# Two more owners, for a package that takes files from both at once.
mkdir -p "$work/x/usr/bin" "$work/y/usr/bin" "$work/both/usr/bin"
mkdir -p "$work/x/usr/share/pkgx" "$work/y/usr/share/pkgy"
printf 'x\n' > "$work/x/usr/bin/x"
printf 'y\n' > "$work/y/usr/bin/y"
# Each keeps a file of its own, so neither disappears when the shared
# one is taken: that case is the last section.
printf 'keep\n' > "$work/x/usr/share/pkgx/keep"
printf 'keep\n' > "$work/y/usr/share/pkgy/keep"
printf 'both-x\n' > "$work/both/usr/bin/x"
printf 'both-y\n' > "$work/both/usr/bin/y"

make_pkg_tree "$work/pkgx_1.0.aeltra" pkgx 1.0 "" "$work/x"
make_pkg_tree "$work/pkgy_1.0.aeltra" pkgy 1.0 "" "$work/y"
make_pkg_tree "$work/both_1.0.aeltra" both 1.0 "Replaces: pkgx, pkgy" "$work/both"

make_pkg_tree "$work/oldpkg_1.0.aeltra" oldpkg 1.0 "" "$work/old"
make_pkg_tree "$work/newpkg_1.0.aeltra" newpkg 1.0 "Replaces: oldpkg" "$work/new"
# The documented idiom: a versioned Breaks beside the Replaces, for a
# file moving out of oldpkg at version 2.  oldpkg 2.0 no longer ships
# it, and is what the Breaks asks the solver to move to.
mkdir -p "$work/old2/usr/share/old"
printf 'kept\n' > "$work/old2/usr/share/old/kept"
make_pkg_tree "$work/oldpkg_2.0.aeltra" oldpkg 2.0 "" "$work/old2"
make_pkg_tree "$work/splitpkg_1.0.aeltra" splitpkg 1.0 "Replaces: oldpkg (<< 2)
Breaks: oldpkg (<< 2)" "$work/new"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

aept_run "$root" install --non-interactive "$work/oldpkg_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing oldpkg failed"
owns oldpkg 'usr/bin/moved' || fail "oldpkg does not claim the path it ships"

# ── the overwrite is allowed, and both packages stay ─────────────────

out=$(aept_run "$root" install --non-interactive "$work/newpkg_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a bare Replaces was refused, exit $rc:
$out"

installed newpkg || fail "newpkg was not installed:
$out"
installed oldpkg || fail "oldpkg was removed; a bare Replaces must not force that:
$out"
grep -q '^new$' "$root/usr/bin/moved" || fail "the file was not taken over"
note "Replaces without Conflicts overwrites, and both packages stay"

# ── the old package stops claiming the path ──────────────────────────

owns newpkg 'usr/bin/moved' || fail "newpkg does not claim the path it took over"
owns oldpkg 'usr/bin/moved' && fail "oldpkg still claims a path it no longer owns"
owns oldpkg 'usr/share/old/kept' \
    || fail "rewriting oldpkg's file list dropped a path it does still own"
note "the path leaves the old package's file list, and nothing else does"

# ── ... so removing it does not delete the file ──────────────────────
#
# The assertion the rewrite exists for.  With a stale file list, oldpkg
# takes usr/bin/moved with it and newpkg is left broken.

out=$(aept_run "$root" remove --non-interactive oldpkg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing oldpkg exited $rc:
$out"

[ -f "$root/usr/bin/moved" ] \
    || fail "removing the old package deleted a file it had handed over:
$out"
grep -q '^new$' "$root/usr/bin/moved" || fail "the handed-over file was replaced"
[ -f "$root/usr/share/old/kept" ] && fail "oldpkg's own file survived its removal"
note "removing the old package leaves the handed-over file alone"

# ── Breaks plus Replaces: the documented split ───────────────────────
#
# Policy's own example is a file moving out of foo into foo-data, with
#
#   Replaces: foo (<< 1.2-3)
#   Breaks:   foo (<< 1.2-3)
#
# Breaks is not Conflicts: it asks for the other package to be *moved
# past* the named version, not removed.  aept hands libsolv a versioned
# conflict, which it can satisfy by upgrading -- so with oldpkg 2.0 in
# the archive, oldpkg survives.  (With no such version to move to, a
# versioned conflict has only removal left; that is libsolv's reading
# and a divergence from dpkg, which would refuse to configure instead.)

{
    packages_stanza oldpkg 2.0 "$work/oldpkg_2.0.aeltra"
    packages_stanza splitpkg 1.0 "$work/splitpkg_1.0.aeltra" "Replaces: oldpkg (<< 2)
Breaks: oldpkg (<< 2)"
} > "$root/var/lib/aept/lists/testrepo"

aept_run "$root" remove --non-interactive newpkg oldpkg >/dev/null 2>&1
aept_run "$root" install --non-interactive "$work/oldpkg_1.0.aeltra" >/dev/null 2>&1 \
    || fail "reinstalling oldpkg 1.0 failed"

out=$(aept_run "$root" install --non-interactive splitpkg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "Breaks plus Replaces was refused, exit $rc:
$out"

installed splitpkg || fail "splitpkg was not installed:
$out"
installed oldpkg || fail "Breaks was read as Conflicts and removed oldpkg outright:
$out"
grep -q '^new$' "$root/usr/bin/moved" \
    || fail "the moved file does not hold the new package's content:
$out"
owns oldpkg 'usr/bin/moved' && fail "oldpkg still claims the moved path"
note "Breaks plus Replaces moves the other package on rather than removing it"

# ── taking over from two packages at once ────────────────────────────
#
# The file lists are rewritten per owner, so a package taking paths from
# several must leave each of them correct.

aept_run "$root" remove --non-interactive splitpkg oldpkg >/dev/null 2>&1
aept_run "$root" install --non-interactive "$work/pkgx_1.0.aeltra" \
    "$work/pkgy_1.0.aeltra" >/dev/null 2>&1 || fail "installing pkgx and pkgy failed"

out=$(aept_run "$root" install --non-interactive "$work/both_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "taking over from two packages exited $rc:
$out"

installed pkgx || fail "pkgx was removed"
installed pkgy || fail "pkgy was removed"
owns pkgx 'usr/bin/x' && fail "pkgx still claims the path it handed over"
owns pkgy 'usr/bin/y' && fail "pkgy still claims the path it handed over"
grep -q '^both-x$' "$root/usr/bin/x" || fail "usr/bin/x was not taken over"
grep -q '^both-y$' "$root/usr/bin/y" || fail "usr/bin/y was not taken over"
note "a package taking paths from two owners disowns them in both lists"

# ── the owner was installed in this same transaction ─────────────────
#
# The package handing the path over need not have been installed before
# the transaction started: it is not in the solver's installed snapshot
# then, only in a repository, and the takeover has to resolve it anyway.

aept_run "$root" remove --non-interactive both pkgx pkgy >/dev/null 2>&1
{
    packages_stanza pkgx 1.0 "$work/pkgx_1.0.aeltra"
    packages_stanza pkgy 1.0 "$work/pkgy_1.0.aeltra"
    packages_stanza both 1.0 "$work/both_1.0.aeltra" "Replaces: pkgx, pkgy"
} > "$root/var/lib/aept/lists/testrepo"

out=$(aept_run "$root" install --non-interactive pkgx pkgy both 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing owner and taker in one transaction exited $rc:
$out"

installed pkgx || fail "pkgx is not installed"
installed both || fail "both is not installed"
owns pkgx 'usr/bin/x' && fail "pkgx still claims a path handed over in the same transaction"
grep -q '^both-x$' "$root/usr/bin/x" || fail "the path was not taken over"
note "a takeover resolves an owner installed in the same transaction"

# ── a file list the rewrite cannot hold a line of ────────────────────
#
# Rewriting another package's .list means reading it, and a line too
# long for the reader must be dropped whole rather than split -- half a
# path is a path, and writing one back would claim a file nobody has.
# Every other entry has to survive.

aept_run "$root" remove --non-interactive both pkgx pkgy >/dev/null 2>&1
aept_run "$root" install --non-interactive "$work/pkgx_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing pkgx failed"

listfile=$root/var/lib/aept/info/pkgx.list
{
    printf './usr/share/pkgx/'
    awk 'BEGIN { while (i++ < 9000) printf "z" }'
    printf '\t0100644\n'
    printf './usr/share/pkgx/after\t0100644\n'
} >> "$listfile"

out=$(aept_run "$root" install --non-interactive "$work/both_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the takeover failed against an over-long list line, exit $rc:
$out"

owns pkgx 'usr/bin/x' && fail "pkgx still claims the path it handed over"
owns pkgx 'usr/share/pkgx/after' \
    || fail "the entry after the over-long line was lost in the rewrite"
grep -q 'zzzz' "$listfile" && fail "a fragment of the over-long line was written back"
note "an over-long line is dropped whole and the rest of the list survives"

# ── a package left with nothing has disappeared ──────────────────────
#
# "If a package is completely replaced in this way, so that dpkg does
# not know of any files it still contains, it is considered to have
# disappeared."  It is not removed -- nothing is left to remove, and no
# prerm runs because nothing knew in advance.  Its postrm is told who
# took over so it can clean up after itself.

provision_shell "$root" || skip "no shell to run maintainer scripts"

mkdir -p "$work/taker/usr/bin"
printf 'taken\n' > "$work/taker/usr/bin/vanisher"

make_pkg_script "$work/vanisher_1.0.aeltra" vanisher 1.0 postrm \
    'printf "%s\n" "$*" > /disappear.args'
make_pkg_tree "$work/taker_2.5.aeltra" taker 2.5 "Replaces: vanisher" "$work/taker"

aept_run "$root" remove --non-interactive both pkgx pkgy >/dev/null 2>&1
aept_run "$root" install --non-interactive "$work/vanisher_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing vanisher failed"
installed vanisher || fail "vanisher is not installed to begin with"

out=$(aept_run "$root" install --non-interactive "$work/taker_2.5.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "taking over the last file exited $rc:
$out"

installed taker || fail "taker was not installed:
$out"
installed vanisher && fail "a package owning no files is still installed:
$out"
[ -f "$root/usr/bin/vanisher" ] \
    || fail "the file was deleted; a disappearance must remove nothing:
$out"
grep -q '^taken$' "$root/usr/bin/vanisher" || fail "the file was not taken over"
note "a package whose last file is taken over disappears"

# The postrm is told what happened, and by whom.
[ -f "$root/disappear.args" ] || fail "the disappearing package's postrm was not run:
$out"
args=$(cat "$root/disappear.args")
[ "$args" = "disappear taker 2.5" ] \
    || fail "postrm got '$args', expected 'disappear taker 2.5'"
note "its postrm is called with disappear, the overwriter and its version"

for ext in list control postrm; do
    [ -f "$root/var/lib/aept/info/vanisher.$ext" ] \
        && fail "vanisher.$ext outlived the disappearance"
done
note "its maintainer scripts and file list are gone"

exit 0
