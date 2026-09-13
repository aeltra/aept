#!/bin/sh
# test_takeover_order.sh - install before removal, when replaced.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# "Conflicts: X" with "Replaces: X" says the new package overwrites X's
# files and what remains of X is removed afterwards (Policy 7.6.2).  The
# order is the point of the pair.  Remove X first and its files are gone
# between the two steps -- for a package holding a shell or the core
# utilities, that gap is a system that cannot finish the transaction.
#
# libsolv orders a conflict's removal first and cannot be asked for the
# other order: its only primitive for "installs over, then removes" is
# obsoletes, which also makes the package an update candidate, which is
# not what Replaces means.  So solver.c does the ordering, and this
# pins both directions of it.
#
# The negative matters as much as the positive.  A bare conflict must
# still remove first: the two packages simply cannot coexist, nothing
# is being handed over, and installing first would put both their file
# sets on disk at once.

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

# Returns the 1-based line on which $2 appears in $1, or nothing.
line_of() { printf '%s\n' "$1" | grep -n "$2" | head -1 | cut -d: -f1; }

# The step order aept reported, as "install remove" line numbers.
order_of() {
    _i=$(line_of "$1" "installing $2")
    _r=$(line_of "$1" "removing $3")
    [ -n "$_i" ] || fail "no install step for $2 reported:
$1"
    [ -n "$_r" ] || fail "no removal step for $3 reported:
$1"
    printf '%s %s\n' "$_i" "$_r"
}

mkdir -p "$work/old/usr/bin" "$work/new/usr/bin" "$work/other/usr/sbin"
printf 'old\n' > "$work/old/usr/bin/tool"
printf 'new\n' > "$work/new/usr/bin/tool"
printf 'other\n' > "$work/other/usr/sbin/otherd"

make_pkg_tree "$work/oldpkg_1.0.aeltra" oldpkg 1.0 "" "$work/old"
make_pkg_tree "$work/newpkg_1.0.aeltra" newpkg 1.0 "Conflicts: oldpkg
Replaces: oldpkg" "$work/new"
# Conflicts and nothing else, and a file set that does not overlap, so
# the install itself has no reason to fail.
make_pkg_tree "$work/rival_1.0.aeltra" rival 1.0 "Conflicts: oldpkg" "$work/other"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

fresh_old() {
    aept_run "$root" remove --non-interactive newpkg rival oldpkg >/dev/null 2>&1
    aept_run "$root" install --non-interactive "$work/oldpkg_1.0.aeltra" >/dev/null 2>&1 \
        || fail "installing oldpkg failed"
}

# ── replaced: the install comes first ────────────────────────────────

fresh_old
out=$(aept_run "$root" install --non-interactive "$work/newpkg_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing newpkg over oldpkg exited $rc:
$out"

set -- $(order_of "$out" newpkg oldpkg)
[ "$1" -lt "$2" ] || fail "oldpkg was removed before newpkg was installed, so its
files were gone in between:
$out"

grep -q '^new$' "$root/usr/bin/tool" \
    || fail "the handed-over file does not hold the new package's content"
note "a replaced package is removed after the one taking its place"

# ── merely conflicting: the removal comes first ──────────────────────

fresh_old
out=$(aept_run "$root" install --non-interactive "$work/rival_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing rival over oldpkg exited $rc:
$out"

set -- $(order_of "$out" rival oldpkg)
[ "$2" -lt "$1" ] || fail "a package that only conflicts was installed before the
package it conflicts with was removed, so both file sets were on disk
at once:
$out"
note "a merely-conflicting package is installed after the removal, as before"

exit 0
