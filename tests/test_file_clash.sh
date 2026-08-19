#!/bin/sh
# test_file_clash.sh - two packages shipping the same path is a
# conflict, and the ways out of it are deliberate: a Replaces:
# declaration, or both sides shipping the same symlink to the same
# directory.  Nothing here may ever cost an installed package a file
# it legitimately owns.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"

# pkga owns usr/bin/shared and a symlink usr/lib/pkga/link -> real.
mkdir -p "$work/a/usr/bin" "$work/a/usr/lib/pkga/real"
printf 'A\n' > "$work/a/usr/bin/shared"
ln -s real "$work/a/usr/lib/pkga/link"
make_pkg_tree "$work/pkga_1.0.aeltra" pkga 1.0 "" "$work/a"

out=$(aept_run "$root" install --non-interactive "$work/pkga_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing pkga exited $rc:
$out"
note "pkga installed, owning usr/bin/shared"

# ── the same path in another package is a refusal ────────────────────

mkdir -p "$work/b/usr/bin"
printf 'B\n' > "$work/b/usr/bin/shared"
make_pkg_tree "$work/pkgb_1.0.aeltra" pkgb 1.0 "" "$work/b"

out=$(aept_run "$root" install --non-interactive "$work/pkgb_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "pkgb was allowed to take pkga's file:
$out"
printf '%s\n' "$out" | grep -q "already provided by package 'pkga'" \
    || fail "the clash was not reported with its owner:
$out"
grep -q '^A$' "$root/usr/bin/shared" \
    || fail "pkga's file did not survive the refused install"
if aept_run "$root" list --installed 2>/dev/null | grep -q '^pkgb '; then
    fail "pkgb was registered despite the clash"
fi
note "clash refused, owner named, pkga's file untouched"

# ── the same symlink to the same directory is shared, not a clash ────
#
# Like directories themselves: two packages may agree that
# usr/lib/pkga/link points at the real directory, and agreeing is not
# fighting.  No Replaces needed.

mkdir -p "$work/d/usr/lib/pkga" "$work/d/usr/bin"
ln -s real "$work/d/usr/lib/pkga/link"
printf 'D\n' > "$work/d/usr/bin/pkgd"
make_pkg_tree "$work/pkgd_1.0.aeltra" pkgd 1.0 "" "$work/d"

out=$(aept_run "$root" install --non-interactive "$work/pkgd_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "pkgd sharing the directory symlink exited $rc:
$out"
[ -f "$root/usr/bin/pkgd" ] || fail "pkgd's own payload did not land"
[ -L "$root/usr/lib/pkga/link" ] || fail "the shared symlink was disturbed"
note "an agreeing directory symlink is shared, not fought over"

# ── a symlink to a *different* target is a clash again ───────────────

mkdir -p "$work/e/usr/lib/pkga" "$work/e/usr/lib/elsewhere"
ln -s elsewhere "$work/e/usr/lib/pkga/link"
make_pkg_tree "$work/pkge_1.0.aeltra" pkge 1.0 "" "$work/e"

out=$(aept_run "$root" install --non-interactive "$work/pkge_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "pkge was allowed to repoint pkga's symlink:
$out"
[ "$(readlink "$root/usr/lib/pkga/link")" = "real" ] \
    || fail "pkga's symlink was repointed by the refused install"
note "a disagreeing symlink is a clash, and the original survives"

# ── a bare Replaces: does not legitimise the takeover ────────────────
#
# Divergence from dpkg, pinned deliberately: libsolv's deb parser drops
# a Replaces that is not accompanied by a Conflicts ("obsoletes only
# count when the packages also conflict", repo_deb.c), and clash.c
# reads the takeover permission from exactly that field.  So the soft
# file-move dpkg allows with Replaces alone is a refusal here, and the
# takeover below needs the full Replaces + Conflicts pair.

mkdir -p "$work/c/usr/bin"
printf 'C\n' > "$work/c/usr/bin/shared"
make_pkg_tree "$work/pkgc_1.0.aeltra" pkgc 1.0 "Replaces: pkga" "$work/c"

out=$(aept_run "$root" install --non-interactive "$work/pkgc_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a bare Replaces: was allowed to take pkga's file:
$out"
grep -q '^A$' "$root/usr/bin/shared" \
    || fail "pkga's file did not survive the bare-Replaces refusal"
note "a bare Replaces: is refused; takeover needs Conflicts too"

# ── Replaces + Conflicts makes the takeover legitimate ───────────────
#
# The solver schedules pkga's removal, and the transaction installs
# pkgc *first* -- so the clash check runs while pkga still owns the
# path, and it is the Replaces declaration that lets it pass.  The
# path is then handed over: pkga's removal afterwards must not delete
# a file that pkgc now owns.

make_pkg_tree "$work/pkgc2_1.0.aeltra" pkgc2 1.0 "Replaces: pkga
Conflicts: pkga" "$work/c"

out=$(aept_run "$root" install --non-interactive "$work/pkgc2_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "pkgc2 with Replaces+Conflicts exited $rc:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^pkgc2 ' \
    || fail "pkgc2 is not registered as installed"
if aept_run "$root" list --installed 2>/dev/null | grep -q '^pkga '; then
    fail "pkga survived being replaced and conflicted away"
fi
grep -q '^C$' "$root/usr/bin/shared" \
    || fail "the handed-over file did not survive pkga's removal"
note "Replaces+Conflicts: pkgc2 takes the path, pkga goes, the file stays"

exit 0
