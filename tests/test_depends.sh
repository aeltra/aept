#!/bin/sh
# test_depends.sh - what a dependency actually requires.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# The grammar is pinned by test_deb.c, which checks that "a (>= 1) | b"
# parses into the right relation.  This is the other half: that the
# relation then *means* something to the solver.  A dependency that
# parses into something weaker than it says is the dangerous failure --
# it installs a package against a version it was never built for, and
# nothing downstream notices.
#
# Alternatives had no functional test at all before this.

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
reset() { aept_run "$root" remove --non-interactive app libfoo libbar >/dev/null 2>&1; }

make_pkg "$work/app.aeltra"        app    1.0
make_pkg "$work/libfoo_1.aeltra"   libfoo 1.0
make_pkg "$work/libfoo_2.aeltra"   libfoo 2.0
make_pkg "$work/libbar.aeltra"     libbar 1.0

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

# ── a version bound is enforced in both directions ───────────────────

reset
{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: libfoo (>= 2.0)"
    packages_stanza libfoo 1.0 "$work/libfoo_1.aeltra"
} > "$list"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a dependency on (>= 2.0) accepted 1.0:
$out"
installed app && fail "app was installed against a version it excludes:
$out"
note "a lower bound refuses a version below it"

reset
{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: libfoo (>= 2.0)"
    packages_stanza libfoo 1.0 "$work/libfoo_1.aeltra"
    packages_stanza libfoo 2.0 "$work/libfoo_2.aeltra"
} > "$list"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a satisfiable lower bound was refused, exit $rc:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^libfoo - 2.0 ' \
    || fail "the version chosen does not satisfy the bound:
$out"
note "and takes the version that satisfies it when one is offered"

# ── a strict upper bound excludes the boundary ───────────────────────
#
# "(<< 2.0)" must not accept 2.0 itself, which is the difference between
# "<<" and "<=" and the reason the parser keeps them apart.

reset
{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: libfoo (<< 2.0)"
    packages_stanza libfoo 2.0 "$work/libfoo_2.aeltra"
} > "$list"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "(<< 2.0) accepted 2.0 itself:
$out"
note "a strict upper bound excludes the version it names"

# ── an alternative is satisfied by either side ───────────────────────

reset
{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: libfoo | libbar"
    packages_stanza libfoo 1.0 "$work/libfoo_1.aeltra"
    packages_stanza libbar 1.0 "$work/libbar.aeltra"
} > "$list"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "an alternative dependency exited $rc:
$out"

n=0
installed libfoo && n=$((n + 1))
installed libbar && n=$((n + 1))
[ "$n" -ge 1 ] || fail "neither side of the alternative was installed:
$out"
note "an alternative is satisfied by one of its sides"

# ── ... and falls back when the first is not available ───────────────
#
# The one that matters: an alternative whose first side cannot be had
# must resolve to the second rather than fail.

reset
{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: libfoo | libbar"
    packages_stanza libbar 1.0 "$work/libbar.aeltra"
} > "$list"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "an alternative did not fall back to its second side, exit $rc:
$out"
installed libbar || fail "the remaining side of the alternative was not installed:
$out"
note "and falls back when the first side is not offered"

# ── neither side available is still a refusal ────────────────────────

reset
{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: libfoo | libbar"
} > "$list"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an alternative with neither side available was accepted:
$out"
note "an alternative with no side available is refused"

exit 0
