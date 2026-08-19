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

# ── a failing trigger: exit 2, recorded, visible in the status ───────
#
# The transaction's work is done, so it is not reported as failed --
# but it is not reported as clean either.  Exit 2 says "succeeded, a
# trigger is owed", the owed directories are on record, and the
# package's status says so.

info=$root/var/lib/aept/info

mkdir -p "$work/bw/usr/share/badtrig"
printf 'b\n' > "$work/bw/usr/share/badtrig/marker"
mk_trig_pkg badtrig_1.0.aeltra badtrig 1.0 "/usr/share/data" \
    '[ -f /ok ] || exit 1
echo "badtrig: $*" >> /trigger.log' "$work/bw"

out=$(aept_run "$root" install --non-interactive "$work/badtrig_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 2 ] || fail "a failing trigger should exit 2, got $rc:
$out"
printf '%s\n' "$out" | grep -q "trigger script for badtrig failed" \
    || fail "the trigger failure was not reported:
$out"
grep -q '^/usr/share/data$' "$info/badtrig.triggers-pending" \
    || fail "the owed directory is not on record:
$(cat "$info/badtrig.triggers-pending" 2>/dev/null)"
grep -q 'Status: install ok triggers-pending' "$info/badtrig.control" \
    || fail "the status does not say triggers-pending:
$(grep Status "$info/badtrig.control")"
aept_run "$root" list --installed 2>/dev/null | grep -q '^badtrig ' \
    || fail "badtrig is not listed as installed despite the completed transaction"
note "failing trigger: exit 2, directory recorded, status triggers-pending"

# ── every following transaction retries -- and reports again ─────────

mkdir -p "$work/u1/opt/u1"
printf 'u\n' > "$work/u1/opt/u1/file"
make_pkg_tree "$work/unrel1_1.0.aeltra" unrel1 1.0 "" "$work/u1"

out=$(aept_run "$root" install --non-interactive "$work/unrel1_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 2 ] || fail "an unrelated transaction should have retried and exited 2, got $rc:
$out"
[ -f "$info/badtrig.triggers-pending" ] || fail "the record vanished without a success"
note "an unrelated transaction retries the pending trigger, still exit 2"

# ── a successful retry clears the record and the status ──────────────

: > "$log"
touch "$root/ok"
mkdir -p "$work/u2/opt/u2"
printf 'u\n' > "$work/u2/opt/u2/file"
make_pkg_tree "$work/unrel2_1.0.aeltra" unrel2 1.0 "" "$work/u2"

out=$(aept_run "$root" install --non-interactive "$work/unrel2_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the transaction after the fix exited $rc:
$out"
grep -q '^badtrig: /usr/share/data$' "$log" \
    || fail "the retry did not hand over the recorded directory:
$(cat "$log")"
[ ! -f "$info/badtrig.triggers-pending" ] || fail "the record survived the success"
grep -q 'Status: install ok installed' "$info/badtrig.control" \
    || fail "the status was not restored to installed:
$(grep Status "$info/badtrig.control")"
note "successful retry: recorded directory delivered, record and status cleared"

# ── aept triggers retries on demand ──────────────────────────────────

mkdir -p "$work/b2/usr/share/badtrig2"
printf 'b\n' > "$work/b2/usr/share/badtrig2/marker"
mk_trig_pkg badtrig2_1.0.aeltra badtrig2 1.0 "/usr/share/data" \
    '[ -f /ok2 ] || exit 1
echo "badtrig2: $*" >> /trigger.log' "$work/b2"

out=$(aept_run "$root" install --non-interactive "$work/badtrig2_1.0.aeltra" 2>&1)
[ "$?" -eq 2 ] || fail "installing badtrig2 should exit 2:
$out"

out=$(aept_run "$root" triggers 2>&1)
rc=$?
[ "$rc" -eq 1 ] || fail "aept triggers with a still-failing script should exit 1, got $rc:
$out"

: > "$log"
touch "$root/ok2"
out=$(aept_run "$root" triggers 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "aept triggers after the fix exited $rc:
$out"
grep -q '^badtrig2: /usr/share/data$' "$log" \
    || fail "aept triggers did not run the pending script:
$(cat "$log")"
[ ! -f "$info/badtrig2.triggers-pending" ] || fail "aept triggers left the record behind"
note "aept triggers: exit 1 while owed, exit 0 and cleared once the script runs"

# ── removing the watcher takes its record with it ────────────────────

mkdir -p "$work/b3/usr/share/badtrig3"
printf 'b\n' > "$work/b3/usr/share/badtrig3/marker"
mk_trig_pkg badtrig3_1.0.aeltra badtrig3 1.0 "/usr/share/data" 'exit 1' "$work/b3"

out=$(aept_run "$root" install --non-interactive "$work/badtrig3_1.0.aeltra" 2>&1)
[ "$?" -eq 2 ] || fail "installing badtrig3 should exit 2:
$out"
[ -f "$info/badtrig3.triggers-pending" ] || fail "no record for badtrig3"

out=$(aept_run "$root" remove --non-interactive badtrig3 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing badtrig3 exited $rc:
$out"
[ ! -f "$info/badtrig3.triggers-pending" ] \
    || fail "the record outlived the package it belongs to"
note "removal clears the pending record"

# ── an upgrade keeps the record and retries with the new script ──────

mkdir -p "$work/b4/usr/share/badtrig4"
printf 'b\n' > "$work/b4/usr/share/badtrig4/marker"
mk_trig_pkg badtrig4_1.0.aeltra badtrig4 1.0 "/usr/share/data" 'exit 1' "$work/b4"

out=$(aept_run "$root" install --non-interactive "$work/badtrig4_1.0.aeltra" 2>&1)
[ "$?" -eq 2 ] || fail "installing badtrig4 1.0 should exit 2:
$out"

: > "$log"
mk_trig_pkg badtrig4_2.0.aeltra badtrig4 2.0 "/usr/share/data" \
    'echo "badtrig4-v2: $*" >> /trigger.log' "$work/b4"

out=$(aept_run "$root" install --non-interactive "$work/badtrig4_2.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrading badtrig4 exited $rc:
$out"
grep -q '^badtrig4-v2: .*\/usr\/share\/data' "$log" \
    || fail "the new version's script was not owed the recorded directory:
$(cat "$log")"
[ ! -f "$info/badtrig4.triggers-pending" ] || fail "the record survived the successful retry"
note "upgrade: record kept, retried with the new script, cleared on success"

exit 0
