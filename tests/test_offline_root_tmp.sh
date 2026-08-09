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

# ── the user namespace is set up before the script runs ──────────────
#
# Running non-root against an offline root, aept unshares a user
# namespace, writes uid_map/setgroups/gid_map and chroots before it
# execs the script interpreter.  All of that happens in the forked
# child, which may only make syscalls, so it is easy to break subtly.
#
# The two failures are distinguishable by exit code, and that is the
# only signal available here: these roots have no /bin/sh, so the exec
# always fails.
#
#   254 (AEPT_EXIT_SETUP_FAILED) — unshare, a map write, or chroot failed
#   255 (AEPT_EXIT_EXEC_FAILED)  — setup succeeded, the interpreter is absent
#
# So 255 is the pass: the child got all the way to exec.

make_aep_script "$work/scripted_1.0.aep" scripted 1.0 postinst 'exit 0'

out=$(aept_run "$root" install --non-interactive \
        "$work/scripted_1.0.aep" 2>&1)

printf '%s\n' "$out" | grep -q 'exit code 254' \
    && fail "namespace setup failed before the interpreter was reached:
$out"

printf '%s\n' "$out" | grep -q 'exit code 255' \
    || fail "expected the child to reach exec and fail there:
$out"
note "user namespace set up and chroot entered; child reached exec"

exit 0
