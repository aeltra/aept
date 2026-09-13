#!/bin/sh
# test_version_order.sh - Debian version ordering, not RPM's.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# libsolv picks its version comparison from pool->disttype, and the
# Debian and RPM functions disagree.  Of the 23,259 adjacent pairs in
# Debian trixie's version strings, 726 order differently -- 3% -- and
# "+" is the usual culprit:
#
#   1.0.1-1  against  1.0+2-1     Debian: newer.  RPM: older.
#
# libsolv's default follows how it was built: Debian's package chooses
# DEB, an upstream cmake build defaults to RPM.  aept therefore asks for
# DISTTYPE_DEB rather than inheriting one, and this checks that the ask
# took effect: an upgrade that only happens under Debian rules.
#
# On a Debian-default libsolv this passes either way, which is the point
# of running the suite under scripts/musl-build.sh as well -- the
# container's libsolv defaults to RPM, and there this test is the one
# that notices.

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

installed_version() {
    aept_run "$root" list --installed 2>/dev/null | awk -v p="$1" '$1 == p { print $3 }'
}

make_pkg "$work/pkg_1.0+2-1.aeltra"  pkg '1.0+2-1'
make_pkg "$work/pkg_1.0.1-1.aeltra"  pkg '1.0.1-1'

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

# The older one by Debian rules is what gets installed first.
{ packages_stanza pkg '1.0+2-1' "$work/pkg_1.0+2-1.aeltra"; } > "$list"
aept_run "$root" install --non-interactive pkg >/dev/null 2>&1 \
    || fail "installing pkg 1.0+2-1 failed"
[ "$(installed_version pkg)" = '1.0+2-1' ] || fail "1.0+2-1 is not the installed version"

# Now offer both.  Under Debian rules 1.0.1-1 is the newer of the two
# and an upgrade must take it; under RPM rules it is older and nothing
# would happen.
{
    packages_stanza pkg '1.0+2-1' "$work/pkg_1.0+2-1.aeltra"
    packages_stanza pkg '1.0.1-1' "$work/pkg_1.0.1-1.aeltra"
} > "$list"

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade exited $rc:
$out"

got=$(installed_version pkg)
[ "$got" = '1.0.1-1' ] || fail "after upgrade the installed version is '$got', expected
'1.0.1-1' -- libsolv is comparing versions by RPM rules, not Debian's:
$out"
note "1.0.1-1 is newer than 1.0+2-1, as Debian orders them"

exit 0
