#!/bin/sh
# test_supersede.sh - Conflicts plus Replaces is not a supersede.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Debian has no field meaning "this package supersedes that one".  RPM's
# Obsoletes means it, and libsolv models it: an obsoleting package is an
# update candidate for the obsoleted one, so a plain upgrade swaps them.
#
# Conflicts plus Replaces is not that.  "Conflicts: X" says X must go for
# this package to be installed; "Replaces: X" says this package may
# overwrite X's files on the way out.  Both describe how a removal the
# user asked for is carried out.  Neither is a reason to start one.
#
# Read as obsoletes, every mail transport agent becomes an upgrade path
# for every other, and an unattended upgrade starts swapping daemons
# nobody named.  That is what this pins shut: the difference between the
# two declarations shows up when a package is installed by name, never
# in what an upgrade decides to do.

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

# Two mail transport agents, in the Debian manner: each ships the same
# kind of thing, and only one may be installed.
for p in exim postfix dovecot; do
    mkdir -p "$work/$p/usr/share/$p"
    printf '%s\n' "$p" > "$work/$p/usr/share/$p/f"
done

make_pkg_tree "$work/exim_1.0.aeltra" exim 1.0 "" "$work/exim"
# postfix conflicts with exim and may take its files over.
make_pkg_tree "$work/postfix_1.0.aeltra" postfix 1.0 "Conflicts: exim
Replaces: exim" "$work/postfix"
# dovecot merely conflicts -- it takes nothing over.
make_pkg_tree "$work/dovecot_1.0.aeltra" dovecot 1.0 "Conflicts: exim" "$work/dovecot"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

fresh_exim() {
    aept_run "$root" remove --non-interactive postfix dovecot exim >/dev/null 2>&1
    aept_run "$root" install --non-interactive exim >/dev/null 2>&1 \
        || fail "installing exim failed"
    installed exim || fail "exim is not installed after a fresh install"
}

# ── Conflicts + Replaces is not an upgrade path ──────────────────────

{
    packages_stanza exim 1.0 "$work/exim_1.0.aeltra"
    packages_stanza postfix 1.0 "$work/postfix_1.0.aeltra" "Conflicts: exim
Replaces: exim"
} > "$list"
fresh_exim

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade with a conflicting-and-replacing package exited $rc:
$out"
installed exim || fail "exim was removed by an upgrade nobody asked for:
$out"
installed postfix && fail "Conflicts plus Replaces was taken as an upgrade path:
$out"
note "Conflicts plus Replaces is not an upgrade path; the machine is left alone"

# ── ... but installing it by name does remove the conflict ───────────
#
# The same two declarations, now that the user has actually asked for
# postfix.  This is the removal Conflicts calls for, and the one
# Replaces describes the file handling of.

out=$(aept_run "$root" install --non-interactive postfix 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing postfix over exim exited $rc:
$out"
installed postfix || fail "postfix was not installed when asked for by name:
$out"
installed exim && fail "exim survived a package that conflicts with it:
$out"
note "asked for by name, it is installed and the conflict is removed"

# ── a bare conflict behaves the same way ─────────────────────────────
#
# dovecot conflicts with exim and replaces nothing.  An upgrade must
# leave the machine alone here too: the two cannot coexist, but that is
# a reason to refuse to install both, not a licence to substitute one.
# Replaces changes how files are handled, never whether this happens.

{
    packages_stanza exim 1.0 "$work/exim_1.0.aeltra"
    packages_stanza dovecot 1.0 "$work/dovecot_1.0.aeltra" "Conflicts: exim"
} > "$list"
fresh_exim

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade with a merely-conflicting package exited $rc:
$out"
installed exim || fail "a package was removed for a conflict nobody asked to resolve:
$out"
installed dovecot && fail "a merely-conflicting package was installed by an upgrade:
$out"
note "a bare conflict is not an upgrade path either"

exit 0
