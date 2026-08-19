#!/bin/sh
# test_pin.sh - a pin that silently fails to hold is how a machine ends
# up on a version somebody deliberately pinned away from.  Every hold
# and every release here is asserted from the installed version, not
# from the pin file alone.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

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
pin_file=$root/var/lib/aept/pinned-packages
mkdir -p "$cache"

version_of() {
    aept_run "$root" list --installed 2>/dev/null | sed -n "s/^$1 - \([^ ]*\).*/\1/p"
}

mkdir -p "$work/t1/usr/share/tool" "$work/t2/usr/share/tool"
printf '1\n' > "$work/t1/usr/share/tool/v"
printf '2\n' > "$work/t2/usr/share/tool/v"
make_pkg_tree "$work/tool_1.0.aeltra" tool 1.0 "" "$work/t1"
make_pkg_tree "$work/tool_2.0.aeltra" tool 2.0 "" "$work/t2"

add_repo "$root" testrepo "$work"
packages_stanza tool 1.0 "$work/tool_1.0.aeltra" > "$list"
cp "$work/tool_1.0.aeltra" "$work/tool_2.0.aeltra" "$cache/"

aept_run "$root" install --non-interactive tool >/dev/null 2>&1 \
    || fail "installing tool 1.0 failed"
[ "$(version_of tool)" = "1.0" ] || fail "starting version is not 1.0"

# ── a pin holds the package back across upgrade ──────────────────────

out=$(aept_run "$root" pin tool 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "pin exited $rc:
$out"
grep -q '^tool 1.0$' "$pin_file" \
    || fail "the pin file does not record tool 1.0:
$(cat "$pin_file" 2>/dev/null)"

{
    packages_stanza tool 1.0 "$work/tool_1.0.aeltra"
    packages_stanza tool 2.0 "$work/tool_2.0.aeltra"
} > "$list"

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade exited $rc:
$out"
[ "$(version_of tool)" = "1.0" ] \
    || fail "the pin did not hold: tool is at $(version_of tool)"
note "pinned tool stays at 1.0 while 2.0 is published"

# ── unpin releases it ────────────────────────────────────────────────

out=$(aept_run "$root" unpin tool 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "unpin exited $rc:
$out"
grep -q '^tool ' "$pin_file" 2>/dev/null && fail "the pin survived unpin"

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade after unpin exited $rc:
$out"
[ "$(version_of tool)" = "2.0" ] \
    || fail "unpinned tool did not upgrade: $(version_of tool)"
note "unpinned tool upgrades to 2.0"

# ── a pin steers a fresh install away from the best version ──────────

aept_run "$root" remove --non-interactive tool >/dev/null 2>&1 \
    || fail "removing tool failed"

out=$(aept_run "$root" pin tool=1.0 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "pin tool=1.0 exited $rc:
$out"
grep -q '^tool 1.0$' "$pin_file" || fail "the explicit pin was not recorded"

out=$(aept_run "$root" install --non-interactive tool 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install of pinned tool exited $rc:
$out"
[ "$(version_of tool)" = "1.0" ] \
    || fail "the pin did not steer the install: got $(version_of tool), not 1.0"
note "a fresh install of pinned tool takes 1.0, not the best available 2.0"

# ── a pin naming a version nobody publishes falls back, loudly ───────

aept_run "$root" remove --non-interactive tool >/dev/null 2>&1 \
    || fail "removing tool again failed"
aept_run "$root" pin tool=9.9 >/dev/null 2>&1 || fail "pin tool=9.9 failed"

out=$(aept_run "$root" install --non-interactive tool 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install with an unsatisfiable pin exited $rc:
$out"
printf '%s\n' "$out" | grep -q "pinned version '9.9' of 'tool' not found" \
    || fail "the fallback was not announced:
$out"
[ "$(version_of tool)" = "2.0" ] \
    || fail "the fallback did not install the best available: $(version_of tool)"
note "an unpublished pinned version warns and falls back to best available"

# ── removing a pinned package drops its pin ──────────────────────────

aept_run "$root" pin tool=2.0 >/dev/null 2>&1 || fail "re-pinning failed"
grep -q '^tool 2.0$' "$pin_file" || fail "the pin was not rewritten in place"
aept_run "$root" remove --non-interactive tool >/dev/null 2>&1 \
    || fail "removing pinned tool failed"
grep -q '^tool ' "$pin_file" 2>/dev/null \
    && fail "the pin outlived the package it was for"
note "removal takes the pin with it"

exit 0
