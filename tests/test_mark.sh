#!/bin/sh
# test_mark.sh - the mark command edits the auto-installed set that
# autoremove reasons from: auto makes a package eligible, manual
# protects it, --all protects everything at once.
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
auto_file=$root/var/lib/aept/auto-installed

is_auto() { grep -q "^$1\$" "$auto_file" 2>/dev/null; }
installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

make_pkg "$work/one_1.0.aeltra" one 1.0
make_pkg "$work/two_1.0.aeltra" two 1.0
aept_run "$root" install --non-interactive \
    "$work/one_1.0.aeltra" "$work/two_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing one and two failed"
is_auto one && fail "an explicitly installed package started out auto"

# ── mark auto makes a package eligible for autoremove ────────────────

out=$(aept_run "$root" mark auto one 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "mark auto exited $rc:
$out"
is_auto one || fail "one is not in the auto set after mark auto"
is_auto two && fail "two was marked along with one"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
[ "$?" -eq 0 ] || fail "autoremove exited non-zero:
$out"
installed one && fail "marking auto did not make one removable"
installed two || fail "two went with it"
note "mark auto: the package became autoremovable, its neighbour did not"

# ── mark manual protects a package again ─────────────────────────────

aept_run "$root" install --non-interactive "$work/one_1.0.aeltra" >/dev/null 2>&1 \
    || fail "reinstalling one failed"
aept_run "$root" mark auto one >/dev/null 2>&1 || fail "re-marking one failed"

out=$(aept_run "$root" mark manual one 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "mark manual exited $rc:
$out"
is_auto one && fail "one is still in the auto set after mark manual"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
[ "$?" -eq 0 ] || fail "autoremove exited non-zero:
$out"
installed one || fail "a manually marked package was autoremoved"
note "mark manual: protected from autoremove again"

# ── mark manual --all clears the whole set ───────────────────────────

aept_run "$root" mark auto one two >/dev/null 2>&1 || fail "marking both failed"
is_auto one && is_auto two || fail "both should be auto now"

out=$(aept_run "$root" mark manual --all 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "mark manual --all exited $rc:
$out"
is_auto one && fail "one survived --all"
is_auto two && fail "two survived --all"
note "mark manual --all: the set is empty"

# ── bare mark manual without names is a usage error ──────────────────

out=$(aept_run "$root" mark manual 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "mark manual with no names should fail:
$out"
note "mark manual without names or --all is refused"

exit 0
