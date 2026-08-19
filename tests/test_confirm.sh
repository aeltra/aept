#!/bin/sh
# test_confirm.sh - the interactive confirmation prompt, on a real
# terminal.  Everything else in the suite runs --non-interactive or
# without a tty, so the termios path in aept_confirm_continue() -- raw
# mode, single keypress, restore -- had never run.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar python3

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

# An autoremove with candidates always asks before removing, which
# makes it the natural prompt to drive: install a package, mark it
# auto, and it is orphaned by definition.
make_pkg "$work/victim_1.0.aeltra" victim 1.0
aept_run "$root" install --non-interactive "$work/victim_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing victim failed"
aept_run "$root" mark auto victim >/dev/null 2>&1 || fail "marking victim auto failed"

drive() { # <answer>
    PTY_PROMPT="Do you want to continue? [Y/n]" \
    python3 "${srcdir:-.}/ptydrive.py" "$1" -- \
        "$AEPT_BIN" -o "$root" -c "$root/etc/aept/aept.conf" autoremove 2>&1
}

# ── 'n' aborts: nothing is removed, and that is a success ────────────

out=$(drive n)
rc=$?
[ "$rc" -eq 0 ] || fail "answering n exited $rc:
$out"
installed victim || fail "victim was removed despite the answer being n"
printf '%s\n' "$out" | grep -q "Do you want to continue" \
    || fail "the prompt never appeared:
$out"
note "answering n: prompt shown, nothing removed, exit 0"

# ── 'y' proceeds ─────────────────────────────────────────────────────
#
# (Enter would take the default -- also yes -- but the prompt reads a
# single raw keypress, and an empty ptydrive response writes nothing.)

out=$(drive y)
rc=$?
[ "$rc" -eq 0 ] || fail "answering y exited $rc:
$out"
installed victim && fail "victim survived the answer y"
note "answering y: victim removed"

exit 0
