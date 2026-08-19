#!/bin/sh
# test_remove.sh - removal edge cases.  The rule under test throughout:
# after any removal, complete or refused, the status database and the
# filesystem agree -- and nothing owned by anybody else is touched.
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
info=$root/var/lib/aept/info

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

# ── plain removal: files, directories, registration, info files ──────

make_pkg "$work/plain_1.0.aeltra" plain 1.0
aept_run "$root" install --non-interactive "$work/plain_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing plain failed"

out=$(aept_run "$root" remove --non-interactive plain 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "plain removal exited $rc:
$out"
installed plain && fail "plain is still registered after removal"
[ ! -f "$root/usr/bin/plain" ] || fail "the payload survived removal"
[ ! -d "$root/usr/bin" ] || fail "the now-empty usr/bin was not removed"
[ ! -f "$info/plain.list" ] || fail "the .list survived removal"
[ ! -f "$info/plain.control" ] || fail "the .control survived removal"
note "plain removal: payload, empty directories and info files all gone"

# ── a directory another package still uses is left alone ─────────────

make_pkg "$work/one_1.0.aeltra" one 1.0
make_pkg "$work/two_1.0.aeltra" two 1.0
aept_run "$root" install --non-interactive "$work/one_1.0.aeltra" "$work/two_1.0.aeltra" \
    >/dev/null 2>&1 || fail "installing one and two failed"

out=$(aept_run "$root" remove --non-interactive one 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing one exited $rc:
$out"
[ ! -f "$root/usr/bin/one" ] || fail "one's payload survived"
[ -f "$root/usr/bin/two" ] || fail "two's payload was taken along"
[ -d "$root/usr/bin" ] || fail "the shared usr/bin was removed while two still uses it"
note "a shared directory survives; only the emptied ones go"

# ── a .list naming a file already gone is not an error ───────────────

rm -f "$root/usr/bin/two"
out=$(aept_run "$root" remove --non-interactive two 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing two with its payload already gone exited $rc:
$out"
installed two && fail "two is still registered"
note "a file already gone does not fail the removal"

# ── a modified conffile is kept; --purge takes it anyway ─────────────

make_pkg_conffile "$work/cfg_1.0.aeltra" cfg 1.0 /etc/cfg.conf original
aept_run "$root" install --non-interactive "$work/cfg_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing cfg failed"
printf 'edited by hand\n' > "$root/etc/cfg.conf"

out=$(aept_run "$root" remove --non-interactive cfg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing cfg exited $rc:
$out"
installed cfg && fail "cfg is still registered"
[ -f "$root/etc/cfg.conf" ] || fail "the user's edited conffile was deleted"
grep -q 'edited by hand' "$root/etc/cfg.conf" \
    || fail "the kept conffile no longer holds the user's edit"
printf '%s\n' "$out" | grep -q "not removing modified conffile" \
    || fail "the kept conffile was not reported:
$out"
note "a modified conffile survives its package"

rm -f "$root/etc/cfg.conf"
aept_run "$root" install --non-interactive "$work/cfg_1.0.aeltra" >/dev/null 2>&1 \
    || fail "reinstalling cfg failed"
printf 'edited again\n' > "$root/etc/cfg.conf"

out=$(aept_run "$root" remove --non-interactive --purge cfg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "purging cfg exited $rc:
$out"
[ ! -f "$root/etc/cfg.conf" ] || fail "--purge left the modified conffile behind"
note "--purge removes even the modified conffile"

# ── an unmodified conffile goes with its package ─────────────────────

aept_run "$root" install --non-interactive "$work/cfg_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing cfg a third time failed"
out=$(aept_run "$root" remove --non-interactive cfg 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing pristine cfg exited $rc:
$out"
[ ! -f "$root/etc/cfg.conf" ] || fail "the pristine conffile was kept"
note "a pristine conffile is removed normally"

# ── a failing prerm aborts the removal, consistently ─────────────────

make_pkg "$work/badpre_1.0.aeltra" badpre 1.0 prerm 1
aept_run "$root" install --non-interactive "$work/badpre_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing badpre failed"

out=$(aept_run "$root" remove --non-interactive badpre 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a failing prerm did not fail the removal:
$out"
printf '%s\n' "$out" | grep -q "prerm failed" \
    || fail "the prerm failure was not reported:
$out"
installed badpre || fail "badpre was deregistered despite the aborted removal"
[ -f "$root/usr/bin/badpre" ] || fail "badpre's payload was removed despite the abort"
[ -f "$info/badpre.list" ] || fail "badpre's .list is gone despite the abort"
note "failing prerm: removal refused, package fully intact"

# ── a failing postrm warns but completes ─────────────────────────────

make_pkg "$work/badpost_1.0.aeltra" badpost 1.0 postrm 1
aept_run "$root" install --non-interactive "$work/badpost_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing badpost failed"

out=$(aept_run "$root" remove --non-interactive badpost 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a failing postrm failed the whole removal:
$out"
printf '%s\n' "$out" | grep -q "postrm failed" \
    || fail "the postrm failure was not reported:
$out"
installed badpost && fail "badpost is still registered"
[ ! -f "$root/usr/bin/badpost" ] || fail "badpost's payload survived"
note "failing postrm: reported, but the removal completes"

# ── removing a depended-on package takes the dependent along ─────────

mkdir -p "$work/lib/usr/bin" "$work/app/usr/bin"
printf 'lib\n' > "$work/lib/usr/bin/lib"
printf 'app\n' > "$work/app/usr/bin/app"
make_pkg_tree "$work/lib_1.0.aeltra" lib 1.0 "" "$work/lib"
make_pkg_tree "$work/app_1.0.aeltra" app 1.0 "Depends: lib" "$work/app"
aept_run "$root" install --non-interactive "$work/lib_1.0.aeltra" "$work/app_1.0.aeltra" \
    >/dev/null 2>&1 || fail "installing lib and app failed"

out=$(aept_run "$root" remove --non-interactive lib 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing depended-on lib exited $rc:
$out"
printf '%s\n' "$out" | grep -q "app" \
    || fail "the dependent's removal was not announced:
$out"
installed lib && fail "lib is still registered"
installed app && fail "app survived the removal of its dependency"
[ ! -f "$root/usr/bin/app" ] || fail "app's payload survived"
note "removing lib takes app along, announced beforehand"

exit 0
