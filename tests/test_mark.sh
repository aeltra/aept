#!/bin/sh
# test_mark.sh - the mark command edits the auto-installed set that
# autoremove reasons from: auto makes a package eligible, manual
# protects it, --all protects everything at once.
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
auto_file=$root/var/lib/aept/auto-installed

is_auto() { grep -q "^$1\$" "$auto_file" 2>/dev/null; }
installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

make_pkg "$work/one_1.0.aeltra" one 1.0
make_pkg "$work/two_1.0.aeltra" two 1.0
aept_run "$root" install --non-interactive \
    "$work/one_1.0.aeltra" "$work/two_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing one and two failed"
is_auto one && fail "an explicitly installed package started out auto"

# ── mark auto makes a package eligible for autoremove ────────────────

out=$(aept_run "$root" mark auto one 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "mark auto exited $rc:
$out"
is_auto one || fail "one is not in the auto set after mark auto"
is_auto two && fail "two was marked along with one"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
[ "$?" -eq 0 ] || fail "autoremove exited non-zero:
$out"
installed one && fail "marking auto did not make one removable"
installed two || fail "two went with it"
note "mark auto: the package became autoremovable, its neighbour did not"

# ── mark manual protects a package again ─────────────────────────────

aept_run "$root" install --non-interactive "$work/one_1.0.aeltra" >/dev/null 2>&1 \
    || fail "reinstalling one failed"
aept_run "$root" mark auto one >/dev/null 2>&1 || fail "re-marking one failed"

out=$(aept_run "$root" mark manual one 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "mark manual exited $rc:
$out"
is_auto one && fail "one is still in the auto set after mark manual"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
[ "$?" -eq 0 ] || fail "autoremove exited non-zero:
$out"
installed one || fail "a manually marked package was autoremoved"
note "mark manual: protected from autoremove again"

# ── mark manual --all clears the whole set ───────────────────────────

aept_run "$root" mark auto one two >/dev/null 2>&1 || fail "marking both failed"
is_auto one && is_auto two || fail "both should be auto now"

out=$(aept_run "$root" mark manual --all 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "mark manual --all exited $rc:
$out"
is_auto one && fail "one survived --all"
is_auto two && fail "two survived --all"
note "mark manual --all: the set is empty"

# ── bare mark manual without names is a usage error ──────────────────

out=$(aept_run "$root" mark manual 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "mark manual with no names should fail:
$out"
note "mark manual without names or --all is refused"

# ── a damaged auto-installed file ────────────────────────────────────
#
# The file is what autoremove reasons from, so a line it cannot parse
# must cost that line and nothing else.  Reading a long line back in
# pieces would invent package names; dropping the file would make every
# auto-installed package look manual and never be collected.

aept_run "$root" mark auto one >/dev/null 2>&1 || fail "marking one auto failed"
{
    printf 'keeper\n'
    awk 'BEGIN { while (i++ < 300) printf "verylongname"; printf "\n" }'
    printf '\n'
    printf 'two\n'
} > "$auto_file"

# mark manual is the operation that rewrites the file, so it is the one
# that has to survive the damage.
aept_run "$root" mark manual two >/dev/null 2>&1 || fail "unmarking two failed"
grep -q '^keeper$' "$auto_file" || fail "an entry before the long line was lost:
$(cat "$auto_file")"
grep -q '^two$' "$auto_file" && fail "mark manual did not remove the entry:
$(cat "$auto_file")"
grep -q 'verylongnameverylongname' "$auto_file" \
    && fail "the unparseable line survived the rewrite:
$(cat "$auto_file")"
note "an unparseable auto-installed line is dropped whole, neighbours kept"

# Nowhere to write: reported, not silently lost.
# "one" is installed and, after the rewrite above, no longer marked --
# both are needed, or the call returns before it opens the file: an
# unknown name is skipped, and an already-marked one short-circuits.
#
# The file itself is made read-only, not its directory: marking appends
# to a file that already exists, and a read-only directory still permits
# that.
chmod 400 "$auto_file"
out=$(aept_run "$root" mark auto one 2>&1)
rc=$?
chmod 600 "$auto_file"
if [ "$rc" -eq 0 ]; then
    note "SKIP: the auto-installed file stayed writable (running as root?)"
else
    case $out in
        *auto-installed*) ;;
        *) fail "an unwritable auto-installed file gave no useful error:
$out" ;;
    esac
    note "an unwritable auto-installed file is reported, not ignored"
fi

# Rewriting needs to create a temporary beside it, which a read-only
# directory does prevent.  The name has to be an installed one, or the
# call skips it before reaching the file at all.
chmod 500 "$root/var/lib/aept"
out=$(aept_run "$root" mark manual one 2>&1)
rc=$?
chmod 700 "$root/var/lib/aept"
if [ "$rc" -eq 0 ]; then
    note "SKIP: the state directory stayed writable (running as root?)"
else
    note "a rewrite with nowhere for its temporary file fails rather than truncating"
fi

exit 0
