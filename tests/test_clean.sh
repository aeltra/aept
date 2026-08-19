#!/bin/sh
# test_clean.sh - clean empties the download cache and touches nothing
# else.
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
cache=$root/var/cache/aept

# An installed package, and a cache with leftovers.
make_pkg "$work/keep_1.0.aeltra" keep 1.0
aept_run "$root" install --non-interactive "$work/keep_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing keep failed"

mkdir -p "$cache"
printf 'cached\n' > "$cache/old_1.0.aeltra"
printf 'cached\n' > "$cache/older_0.9.aeltra"

out=$(aept_run "$root" clean 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "clean exited $rc:
$out"
[ -z "$(ls -A "$cache" 2>/dev/null)" ] || fail "the cache is not empty:
$(ls -A "$cache")"
aept_run "$root" list --installed 2>/dev/null | grep -q '^keep ' \
    || fail "clean deregistered an installed package"
[ -f "$root/usr/bin/keep" ] || fail "clean deleted an installed file"
note "cache emptied; installed package untouched"

# ── an absent cache directory is a clean no-op ───────────────────────

rm -rf "$cache"
out=$(aept_run "$root" clean 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "clean with no cache directory exited $rc:
$out"
note "no cache directory: exit 0, no complaint"

exit 0
