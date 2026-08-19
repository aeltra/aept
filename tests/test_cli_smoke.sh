#!/bin/sh
# test_cli_smoke.sh - the CLI's argument surface: every subcommand's
# --help, the argument-required refusals, unknown commands and options,
# and which stream and status each answer uses.  Deliberately the last
# and cheapest test in the plan: main.c's tier is capped at 60% because
# past this the uncovered lines are the help text itself.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"

COMMANDS="update install remove autoremove upgrade clean triggers list show files owns mark pin unpin print-architecture"

# ── --help: exit 0, usage on stdout, silence on stderr ───────────────

for cmd in $COMMANDS; do
    out=$(aept_run "$root" "$cmd" --help 2>"$work/err")
    rc=$?
    [ "$rc" -eq 0 ] || fail "$cmd --help exited $rc"
    printf '%s\n' "$out" | grep -q "Usage: aept" \
        || fail "$cmd --help printed no usage:
$out"
    [ -s "$work/err" ] && fail "$cmd --help wrote to stderr:
$(cat "$work/err")"
done
note "--help for all $(echo $COMMANDS | wc -w) subcommands: exit 0, usage on stdout"

aept_run "$root" --help >/dev/null 2>"$work/err" \
    || fail "the global --help failed"
[ -s "$work/err" ] && fail "the global --help wrote to stderr"
note "global --help: exit 0"

# ── a bad option: exit 1, usage on stderr, not stdout ────────────────

for cmd in $COMMANDS; do
    out=$(aept_run "$root" "$cmd" --no-such-option 2>"$work/err")
    rc=$?
    [ "$rc" -eq 1 ] || fail "$cmd --no-such-option exited $rc, not 1"
    grep -q "Usage: aept" "$work/err" \
        || fail "$cmd with a bad option printed no usage on stderr:
$(cat "$work/err")"
    [ -n "$out" ] && fail "$cmd with a bad option wrote to stdout:
$out"
done
note "a bad option: exit 1, usage on stderr, stdout clean"

# ── commands that need arguments refuse to run without them ──────────

for cmd in install remove show files owns pin unpin; do
    out=$(aept_run "$root" "$cmd" 2>&1)
    rc=$?
    [ "$rc" -eq 1 ] || fail "bare '$cmd' exited $rc, not 1"
    printf '%s\n' "$out" | grep -q "requires" \
        || fail "bare '$cmd' did not say what it requires:
$out"
done
note "argument-less install/remove/show/files/owns/pin/unpin all refuse"

out=$(aept_run "$root" mark 2>&1)
rc=$?
[ "$rc" -eq 1 ] || fail "bare 'mark' exited $rc, not 1"
out=$(aept_run "$root" mark frobnicate x 2>&1)
rc=$?
[ "$rc" -eq 1 ] || fail "'mark frobnicate' exited $rc, not 1"
note "mark without or with an unknown action refuses"

# ── the unknown and the absent command ───────────────────────────────

out=$(aept_run "$root" no-such-command 2>"$work/err")
rc=$?
[ "$rc" -eq 1 ] || fail "an unknown command exited $rc, not 1"
grep -q "unknown command" "$work/err" \
    || fail "the unknown command was not named:
$(cat "$work/err")"

out=$(aept_run "$root" 2>"$work/err")
rc=$?
[ "$rc" -eq 1 ] || fail "no command at all exited $rc, not 1"
grep -q "Usage: aept" "$work/err" \
    || fail "no-command printed no usage on stderr"
note "unknown and missing commands: exit 1, explained on stderr"

# ── the read-only commands run against an empty root ─────────────────

out=$(aept_run "$root" print-architecture 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "print-architecture exited $rc:
$out"
[ "$out" = "all" ] || fail "print-architecture printed '$out', not the configured 'all'"

out=$(aept_run "$root" list 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "list against an empty root exited $rc:
$out"

out=$(aept_run "$root" show absent 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "show of an absent package exited 0"
printf '%s\n' "$out" | grep -qi "not found\|no such" \
    || fail "show absent did not say so:
$out"

out=$(aept_run "$root" owns /nowhere 2>&1)
rc=$?
[ "$rc" -eq 1 ] || fail "owns of an unowned path exited $rc, not 1"
note "read-only commands behave against an empty root"

exit 0
