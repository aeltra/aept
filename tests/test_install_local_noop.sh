#!/bin/sh
# test_install_local_noop.sh - installing a local .aeltra that is already
# installed must do nothing, not upgrade the whole system.
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

make_aeltra "$work/app_1.0.aeltra"   app   1.0
make_aeltra "$work/other_1.0.aeltra" other 1.0
make_aeltra "$work/other_2.0.aeltra" other 2.0

out=$(aept_run "$root" install --non-interactive \
        "$work/app_1.0.aeltra" "$work/other_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "setup install exited $rc:
$out"

# A repository offering a newer "other" — something a stray upgrade-all
# would visibly pick up.
add_repo "$root" testrepo "$work"
packages_stanza other 2.0 "$work/other_2.0.aeltra" \
    > "$root/var/lib/aept/lists/testrepo"

# ── the regression ───────────────────────────────────────────────────
#
# app 1.0 is already installed, so the local package is filtered out and
# the job list ends up empty.  The solver reads an empty job list as
# "upgrade everything", so this used to schedule other 1.0 -> 2.0 — a
# package the user never mentioned — without prompting.

out=$(aept_run "$root" install -n --non-interactive "$work/app_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install of an already-installed file exited $rc:
$out"

printf '%s\n' "$out" | grep -q 'UPGRADED' \
    && fail "installing an already-installed local file scheduled upgrades:
$out"

printf '%s\n' "$out" | grep -q 'nothing to do' \
    || fail "expected 'nothing to do':
$out"
note "already-installed local file is a no-op"

# ── upgrade-all must still work ──────────────────────────────────────
#
# The guard above must not disturb the path that legitimately relies on
# an empty job list.

out=$(aept_run "$root" upgrade -n --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade exited $rc:
$out"
printf '%s\n' "$out" | grep -q 'UPGRADED' \
    || fail "aept upgrade no longer schedules available upgrades:
$out"
printf '%s\n' "$out" | grep -q 'other' \
    || fail "aept upgrade did not pick up other 2.0:
$out"
note "aept upgrade still schedules a full upgrade"

# ── a local file that is not installed yet still installs ────────────

out=$(aept_run "$root" install -n --non-interactive "$work/other_2.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install of a newer local file exited $rc:
$out"
printf '%s\n' "$out" | grep -qE 'UPGRADED|NEW packages' \
    || fail "a local file that is not installed yet was not scheduled:
$out"
note "a not-yet-installed local file is still scheduled"

exit 0
