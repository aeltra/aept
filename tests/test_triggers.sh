#!/bin/sh
# test_triggers.sh - directory-watch triggers actually firing: on the
# directories a transaction touched, once per transaction, and never on
# ones it did not.  Until now the suite only parsed the trigger file;
# no test had ever run one.
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
provision_shell "$root" || skip "cannot provision a shell into the offline root"

log=$root/trigger.log
: > "$log"

# mk_trig_pkg <out> <name> <version> <patterns> <script-body> <tree>
#
# A package carrying a triggers file (one pattern per line), a trigger
# script, and the given payload tree.
mk_trig_pkg() {
    _out=$1 _name=$2 _ver=$3 _patterns=$4 _body=$5 _tree=$6
    _d=$(mktemp -d) || fail "mktemp failed"

    mkdir -p "$_d/c"
    printf 'Package: %s\nVersion: %s\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' \
        "$_name" "$_ver" > "$_d/c/control"
    printf '%s\n' "$_patterns" > "$_d/c/triggers"
    printf '#!/bin/sh\n%s\n' "$_body" > "$_d/c/trigger"
    chmod 755 "$_d/c/trigger"

    tar czf "$_d/control.tar.gz" -C "$_d/c" control triggers trigger || fail "tar control"
    tar czf "$_d/data.tar.gz"    -C "$_tree" .                       || fail "tar data"
    printf '2.0\n' > "$_d/debian-binary"

    ( cd "$_d" && ar rc "$work/$_out" debian-binary control.tar.gz data.tar.gz ) \
        || fail "ar failed"
    rm -rf "$_d"
}

# The watcher: interested in /usr/share/data, its own payload elsewhere.
mkdir -p "$work/w/usr/share/watcher"
printf 'w\n' > "$work/w/usr/share/watcher/marker"
mk_trig_pkg watcher_1.0.aeltra watcher 1.0 "/usr/share/data" \
    'echo "watcher: $*" >> /trigger.log' "$work/w"

# ── installing the watcher alone fires nothing ───────────────────────
#
# Its own files live elsewhere and /usr/share/data does not exist yet,
# so there is nothing to report.

aept_run "$root" install --non-interactive "$work/watcher_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing watcher failed"
[ -s "$log" ] && fail "the trigger fired with nothing touched:
$(cat "$log")"
note "watcher installed; trigger silent, as nothing it watches was touched"

# ── one transaction touching the watched directory fires once ────────

mkdir -p "$work/p1/usr/share/data" "$work/p2/usr/share/data"
printf '1\n' > "$work/p1/usr/share/data/one"
printf '2\n' > "$work/p2/usr/share/data/two"
make_pkg_tree "$work/pay1_1.0.aeltra" pay1 1.0 "" "$work/p1"
make_pkg_tree "$work/pay2_1.0.aeltra" pay2 1.0 "" "$work/p2"

aept_run "$root" install --non-interactive \
    "$work/pay1_1.0.aeltra" "$work/pay2_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing pay1 and pay2 failed"

[ "$(grep -c '^watcher:' "$log")" -eq 1 ] \
    || fail "the trigger should have fired exactly once for the transaction:
$(cat "$log")"
grep -q '^watcher: /usr/share/data$' "$log" \
    || fail "the trigger did not receive the touched directory once:
$(cat "$log")"
note "two packages, one watched directory: one firing, one argument"

# ── a transaction elsewhere does not fire it ─────────────────────────

: > "$log"
mkdir -p "$work/o/opt/other"
printf 'o\n' > "$work/o/opt/other/file"
make_pkg_tree "$work/other_1.0.aeltra" other 1.0 "" "$work/o"

aept_run "$root" install --non-interactive "$work/other_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing other failed"
[ -s "$log" ] && fail "the trigger fired for an unrelated directory:
$(cat "$log")"
note "an unrelated transaction leaves the trigger alone"

# ── glob patterns match by fnmatch ───────────────────────────────────

mkdir -p "$work/g/usr/share/g-modules"
printf 'g\n' > "$work/g/usr/share/g-modules/mod"
mkdir -p "$work/gw/usr/share/globber"
printf 'g\n' > "$work/gw/usr/share/globber/marker"
mk_trig_pkg globber_1.0.aeltra globber 1.0 "/usr/share/g-*" \
    'echo "globber: $*" >> /trigger.log' "$work/gw"

aept_run "$root" install --non-interactive "$work/globber_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing globber failed"
: > "$log"

make_pkg_tree "$work/gpay_1.0.aeltra" gpay 1.0 "" "$work/g"
aept_run "$root" install --non-interactive "$work/gpay_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing gpay failed"
grep -q '^globber: /usr/share/g-modules$' "$log" \
    || fail "the glob pattern did not match the touched directory:
$(cat "$log")"
note "a glob pattern fires on a matching directory"

# ── a fresh watcher self-fires on a watched dir that already exists ──
#
# selfwatch arrives after /usr/share/data exists.  Its own payload does
# not touch it, but a freshly installed package's concrete pattern is
# looked up on disk, so it is told about the state of the world it just
# became interested in.

: > "$log"
mkdir -p "$work/sw/usr/share/selfwatch"
printf 's\n' > "$work/sw/usr/share/selfwatch/marker"
mk_trig_pkg selfwatch_1.0.aeltra selfwatch 1.0 "/usr/share/data" \
    'echo "selfwatch: $*" >> /trigger.log' "$work/sw"

aept_run "$root" install --non-interactive "$work/selfwatch_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing selfwatch failed"
grep -q '^selfwatch: /usr/share/data$' "$log" \
    || fail "the fresh watcher was not told about the existing directory:
$(cat "$log")"
note "a fresh watcher is told about a watched directory that already exists"

# ── removal touches directories too ──────────────────────────────────

: > "$log"
aept_run "$root" remove --non-interactive pay1 >/dev/null 2>&1 \
    || fail "removing pay1 failed"
grep -q '^watcher: /usr/share/data$' "$log" \
    || fail "the removal did not fire the watcher:
$(cat "$log")"
note "removing a package fires the triggers watching what it touched"

# ── a failing trigger script is an error, not a failed transaction ───
#
# Pinned as observed: the transaction's work is already done when the
# triggers run, so a script failure is reported loudly but does not
# turn a completed install into a reported failure.

mkdir -p "$work/bw/usr/share/badtrig"
printf 'b\n' > "$work/bw/usr/share/badtrig/marker"
mk_trig_pkg badtrig_1.0.aeltra badtrig 1.0 "/usr/share/data" 'exit 1' "$work/bw"

out=$(aept_run "$root" install --non-interactive "$work/badtrig_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a failing trigger failed the whole install (exit $rc):
$out"
printf '%s\n' "$out" | grep -q "trigger script for badtrig failed" \
    || fail "the trigger failure was not reported:
$out"
note "a failing trigger is reported and the transaction still succeeds"

exit 0
