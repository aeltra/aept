#!/bin/sh
# test_supersede.sh - when one package takes another's place.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Debian says this with two fields, and the pair means more than either
# alone (Policy 7.6): Replaces on its own is permission to overwrite
# another package's files, and only Replaces *together with* Conflicts
# on the same package means "that package should go, and I take its
# place".  test_file_clash.sh holds that distinction down at install
# time, where it decides who owns a path.
#
# This is the other half: what it means for resolution.  A supersede
# makes a package an update candidate for the one it supersedes, so a
# plain "aept upgrade" -- which asks for nothing in particular -- will
# swap them.  That is correct and useful: it is how a renamed package
# reaches machines that have the old name.
#
# Which makes the negative just as important.  A package that merely
# conflicts must NOT be offered that way, or any two packages declaring
# they cannot coexist become upgrade paths for each other, and an
# unattended upgrade starts replacing things nobody asked it to.

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
# postfix supersedes exim: it conflicts with it AND replaces it.
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

# ── a supersede is an upgrade path ───────────────────────────────────

{
    packages_stanza exim 1.0 "$work/exim_1.0.aeltra"
    packages_stanza postfix 1.0 "$work/postfix_1.0.aeltra" "Conflicts: exim
Replaces: exim"
} > "$list"
fresh_exim

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade with a superseding package exited $rc:
$out"
installed postfix || fail "the superseding package was not installed:
$out"
installed exim && fail "the superseded package is still installed:
$out"
note "a supersede is taken by a plain upgrade, and the old package goes"

# ── a bare conflict is not ───────────────────────────────────────────
#
# dovecot conflicts with exim and replaces nothing.  An upgrade must
# leave the machine alone: the two cannot coexist, but that is a reason
# to refuse to install both, not a licence to substitute one.

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
note "a bare conflict is not an upgrade path; the machine is left alone"

exit 0
