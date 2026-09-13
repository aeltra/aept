#!/bin/sh
# test_conflicts.sh - what a conflict forbids, and what it does not.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Conflicts (Policy 7.4) forbids coexistence; Breaks (7.3) forbids it
# only for the versions it names, asking for the other package to be
# moved on rather than removed.  aept hands libsolv a versioned conflict
# for both, so the range is what separates them.
#
# The negative is the point here.  A versioned conflict that fires
# outside its range removes packages nobody asked about, and that is a
# failure nothing else in the suite would catch: test_supersede.sh and
# test_replaces_overwrite.sh both use conflicts that are *meant* to fire.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
cache=$root/var/cache/aept
list=$root/var/lib/aept/lists/testrepo
mkdir -p "$cache"

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }
version_of() { aept_run "$root" list --installed 2>/dev/null | awk -v p="$1" '$1 == p { print $3 }'; }
reset() { aept_run "$root" remove --non-interactive newpkg old >/dev/null 2>&1; }

for p in old newpkg; do
    mkdir -p "$work/$p/usr/share/$p"
    printf '%s\n' "$p" > "$work/$p/usr/share/$p/f"
done

make_pkg_tree "$work/old_1.aeltra" old 1.0 "" "$work/old"
make_pkg_tree "$work/old_2.aeltra" old 2.0 "" "$work/old"
make_pkg_tree "$work/conflicts_any.aeltra" newpkg 1.0 "Conflicts: old" "$work/newpkg"
make_pkg_tree "$work/conflicts_old.aeltra" newpkg 1.0 "Conflicts: old (<< 2.0)" "$work/newpkg"
make_pkg_tree "$work/breaks_old.aeltra"    newpkg 1.0 "Breaks: old (<< 2.0)" "$work/newpkg"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

install_old() {
    reset
    aept_run "$root" install --non-interactive "$work/old_$1.aeltra" >/dev/null 2>&1 \
        || fail "installing old $1 failed"
    installed old || fail "old is not installed to begin with"
}

# ── an unversioned conflict fires whatever the version ───────────────

install_old 1
out=$(aept_run "$root" install --non-interactive "$work/conflicts_any.aeltra" 2>&1)
[ $? -eq 0 ] || fail "installing over an unversioned conflict failed:
$out"
installed old && fail "an unversioned conflict did not remove the package:
$out"
note "an unversioned conflict removes the package whatever its version"

# ── a versioned conflict fires inside its range ──────────────────────

install_old 1
out=$(aept_run "$root" install --non-interactive "$work/conflicts_old.aeltra" 2>&1)
[ $? -eq 0 ] || fail "installing over a versioned conflict failed:
$out"
installed old && fail "a conflict on (<< 2.0) did not remove 1.0:
$out"
note "a versioned conflict removes a version inside its range"

# ── ... and not outside it ───────────────────────────────────────────

install_old 2
out=$(aept_run "$root" install --non-interactive "$work/conflicts_old.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a conflict that should not apply refused the install, exit $rc:
$out"
installed old || fail "a conflict on (<< 2.0) removed 2.0, which it does not name:
$out"
installed newpkg || fail "newpkg was not installed:
$out"
note "and leaves a version outside its range alone"

# ── Breaks is satisfied by moving the other package on ───────────────
#
# "Breaks: old (<< 2.0)" with old 1.0 installed and 2.0 offered: the
# ask is an upgrade, not a removal.  (With no 2.0 to move to, a
# versioned conflict has only removal left -- libsolv's reading, and a
# divergence from dpkg, which would refuse to configure instead.)

reset
{
    packages_stanza old 2.0 "$work/old_2.aeltra"
    packages_stanza newpkg 1.0 "$work/breaks_old.aeltra" "Breaks: old (<< 2.0)"
} > "$list"

aept_run "$root" install --non-interactive "$work/old_1.aeltra" >/dev/null 2>&1 \
    || fail "installing old 1.0 failed"

out=$(aept_run "$root" install --non-interactive newpkg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "Breaks with an upgrade available exited $rc:
$out"
installed old || fail "Breaks removed the package instead of moving it on:
$out"
[ "$(version_of old)" = "2.0" ] \
    || fail "old is at $(version_of old), expected 2.0 after Breaks asked it to move:
$out"
note "Breaks is satisfied by upgrading past the range it names"

exit 0
