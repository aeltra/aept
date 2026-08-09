#!/bin/sh
# test_offline_root_tmp.sh - control archives must be unpacked inside the
# offline root, so that maintainer scripts remain reachable after chroot.
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
make_aep "$work/good_1.0.aep" good 1.0

# The offline root deliberately has no /tmp yet: a root that is still
# being bootstrapped will not have one, and aept has to cope.
[ -d "$root/tmp" ] && fail "fixture error: $root/tmp should not exist yet"

out=$(aept_run "$root" -v install --non-interactive "$work/good_1.0.aep" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install exited $rc:
$out"

# The control archive must have been unpacked under the offline root.
printf '%s\n' "$out" | grep -q "extracting '$root/tmp/aept-" \
    || fail "control archive was not unpacked under the offline root:
$out"

# Anything landing on the host /tmp instead would be invisible to a
# chrooted script interpreter.
if printf '%s\n' "$out" | grep -q "extracting '/tmp/aept-"; then
    fail "control archive was unpacked on the host /tmp:
$out"
fi

[ -d "$root/tmp" ] || fail "$root/tmp was not created"
note "control unpacked inside the offline root; $root/tmp created"

# The per-package scratch directory must not be left behind.
leftover=$(find "$root/tmp" -maxdepth 1 -name 'aept-*' 2>/dev/null | wc -l)
[ "$leftover" -eq 0 ] || fail "$leftover scratch directories left in $root/tmp"
note "scratch directory cleaned up"

exit 0
