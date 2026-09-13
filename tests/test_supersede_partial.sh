#!/bin/sh
# test_supersede_partial.sh - Conflicts and Replaces that do not name
# the same packages.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Conflicts and Replaces are separate statements, and a package commonly
# makes them about different packages:
#
#   Package: chrony
#   Conflicts: ntp                 # cannot run two NTP daemons
#   Replaces: chrony-legacy        # took over the renamed package
#
# test_supersede.sh pins the case where both name the same package.
# These are the shapes where they do not: disjoint lists, and lists that
# overlap in part.  Neither may turn into an upgrade path, and the
# partial case is the one where a reader that pairs the two lists by
# position rather than by name picks the wrong package -- the one merely
# conflicted with goes, and the one actually replaced is left alone.
#
# libsolv derives obsoletes this way (ext/repo_deb.c, "obsoletes only
# count when the packages also conflict", which compares an entry
# against itself); 482 of the 4069 packages in Debian trixie that carry
# both fields come out with an obsoletes their packager never wrote.
# aept parses control stanzas itself and derives no obsoletes at all, so
# neither shape reaches the solver as a supersede.

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

for p in ntp chrony; do
    mkdir -p "$work/$p/usr/share/$p"
    printf '%s\n' "$p" > "$work/$p/usr/share/$p/f"
done
make_pkg_tree "$work/ntp_1.0.aeltra" ntp 1.0 "" "$work/ntp"
make_pkg_tree "$work/chrony_1.0.aeltra" chrony 1.0 "Conflicts: ntp
Replaces: chrony-legacy" "$work/chrony"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"
{
    packages_stanza ntp 1.0 "$work/ntp_1.0.aeltra"
    packages_stanza chrony 1.0 "$work/chrony_1.0.aeltra" "Conflicts: ntp
Replaces: chrony-legacy"
} > "$list"

aept_run "$root" install --non-interactive ntp >/dev/null 2>&1 \
    || fail "installing ntp failed"
installed ntp || fail "ntp is not installed after a fresh install"

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade exited $rc:
$out"

installed ntp || fail "ntp was removed by an upgrade nobody asked for:
$out"
installed chrony && fail "disjoint Conflicts and Replaces were taken as a supersede:
$out"
note "disjoint Conflicts and Replaces do not make a supersede"

# ── the lists overlap, but only partly ───────────────────────────────
#
# A package commonly conflicts with several alternatives while taking
# over the files of just one.  The supersede is the overlap -- libbar
# here -- and libfoo is only an incompatibility.  Pairing the lists by
# position obsoletes libfoo instead: the wrong one goes, and the one
# actually replaced is not superseded at all.

for p in libfoo libbar merged; do
    mkdir -p "$work/$p/usr/share/$p"
    printf '%s\n' "$p" > "$work/$p/usr/share/$p/f"
done
make_pkg_tree "$work/libfoo_1.0.aeltra" libfoo 1.0 "" "$work/libfoo"
make_pkg_tree "$work/libbar_1.0.aeltra" libbar 1.0 "" "$work/libbar"
make_pkg_tree "$work/merged_1.0.aeltra" merged 1.0 "Conflicts: libfoo, libbar
Replaces: libbar" "$work/merged"
cp "$work"/*.aeltra "$cache/"
{
    packages_stanza libfoo 1.0 "$work/libfoo_1.0.aeltra"
    packages_stanza libbar 1.0 "$work/libbar_1.0.aeltra"
    packages_stanza merged 1.0 "$work/merged_1.0.aeltra" "Conflicts: libfoo, libbar
Replaces: libbar"
} > "$list"

aept_run "$root" remove --non-interactive ntp chrony >/dev/null 2>&1
aept_run "$root" install --non-interactive libfoo >/dev/null 2>&1 \
    || fail "installing libfoo failed"
out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade exited $rc:
$out"

installed libfoo || fail "the package merely conflicted with was removed:
$out"
installed merged && fail "a package was superseded by one that only conflicts with it:
$out"
note "a package is not superseded by one that only conflicts with it"

exit 0
