#!/bin/sh
# test_install_failure.sh - a failing maintainer script must not be
# reported as a successful install.
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

make_aeltra "$work/good_1.0.aeltra"   good   1.0
make_aeltra "$work/badpre_1.0.aeltra" badpre 1.0 preinst 1

# ── a healthy package installs, registers and lands on disk ──────────

out=$(aept_run "$root" install --non-interactive "$work/good_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "healthy install exited $rc:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^good ' \
    || fail "healthy package was not registered as installed"
[ -f "$root/usr/bin/good" ] \
    || fail "healthy package did not place its payload"
note "healthy install: ok"

# ── a package whose preinst fails must not look like a success ───────
#
# The exact exit code of the child is not asserted: until tmp_dir is
# prefixed with the offline root, the control archive is unpacked
# outside the chroot and the script fails to exec (255) rather than
# running and exiting 1.  Either way the install must be reported as a
# failure, which is what regressed.

out=$(aept_run "$root" install --non-interactive "$work/badpre_1.0.aeltra" 2>&1)
rc=$?

[ "$rc" -ne 0 ] \
    || fail "aept exited 0 despite the preinst failing:
$out"

printf '%s\n' "$out" | grep -q 'preinst script for .* failed' \
    || fail "no preinst failure was reported:
$out"

if aept_run "$root" list --installed 2>/dev/null | grep -q '^badpre '; then
    fail "package was registered as installed despite the preinst failing"
fi

[ -f "$root/usr/bin/badpre" ] \
    && fail "package payload was installed despite the preinst failing"

note "failing preinst: reported as a failure and not registered"
exit 0
