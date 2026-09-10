#!/bin/sh
# test_option_order.sh - where an option may appear on the command line.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Two rules, and they pull in opposite directions (see the OPTS_DISPATCH
# / OPTS_LEAF block in main.c):
#
#   a leaf parser permutes, so an option is an option wherever it stands;
#   a dispatching parser stops at the first non-option, so the inner
#   parser's options reach it unread.
#
# Both used to come from whatever getopt state the previous parse had
# left behind, which made them differ between glibc and musl -- silently
# for the leaves, and fatally for "mark manual --all", which musl
# rejected outright.  These cases pin the rules on both.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"

# mark reads the auto-installed set, and new_root writes only the config;
# its absence is an error unrelated to anything tested here.
mkdir -p "$root/var/lib/aept"
: > "$root/var/lib/aept/auto-installed"

# ── leaves permute ───────────────────────────────────────────────────

# An unknown option after an operand must be rejected, not quietly
# absorbed into the operands.  This is the case that regressed: without
# permutation "--bogus" became a package name and the command reported
# "nothing to do", so a typo did nothing and said nothing.
out=$(aept_run "$root" remove nosuchpkg --bogus 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an unknown option after an operand was accepted:
$out"
case $out in
    *unrecognized*|*invalid*) ;;
    *) fail "expected an unrecognized-option error, got:
$out" ;;
esac
note "leaf: an unknown option after an operand is rejected"

# And a real one after an operand takes effect.  --help is the one whose
# effect needs no repository to observe.
out=$(aept_run "$root" remove nosuchpkg --help 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "--help after an operand exited $rc:
$out"
case $out in
    Usage:*) ;;
    *) fail "--help after an operand printed no usage:
$out" ;;
esac
note "leaf: a real option after an operand takes effect"

# Before the operand, unchanged.
out=$(aept_run "$root" remove --help nosuchpkg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "--help before an operand exited $rc:
$out"
note "leaf: a real option before an operand still takes effect"

# ── dispatchers do not ───────────────────────────────────────────────

# "--all" belongs to cmd_mark_manual().  cmd_mark() must stop at the
# action word and never look at it; when it did look, it died with
# "unrecognized option: all".
out=$(aept_run "$root" mark manual --all 2>&1)
rc=$?
case $out in
    *unrecognized*|*invalid*) fail "cmd_mark read an option that was not its own:
$out" ;;
esac
[ "$rc" -eq 0 ] || fail "mark manual --all exited $rc:
$out"
note "dispatcher: the inner parser's option reaches it"

# Same for the global parser: an option of the subcommand, placed after
# the subcommand, must not be read by main().
out=$(aept_run "$root" remove --noaction nosuchpkg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "remove --noaction exited $rc:
$out"
note "dispatcher: main() leaves the subcommand's options alone"

# A genuinely unknown action is still an error, rather than being taken
# for an operand.
out=$(aept_run "$root" mark sideways 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an unknown mark action was accepted:
$out"
note "dispatcher: an unknown action is refused"

exit 0
