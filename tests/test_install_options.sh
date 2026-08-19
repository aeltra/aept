#!/bin/sh
# test_install_options.sh - the install paths off the happy road: a
# package the index cannot point at, one the server no longer has,
# --keep-going past a failure, --no-cache, and a tmp_dir on another
# filesystem.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools python3 ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"

make_pkg "$repo/foo_1.0.aeltra" foo 1.0
make_pkg "$repo/gone_1.0.aeltra" gone 1.0

{
    packages_stanza foo 1.0 "$repo/foo_1.0.aeltra"
    packages_stanza gone 1.0 "$repo/gone_1.0.aeltra"
    # A stanza with no Filename: the index names a package it gives no
    # way to fetch.
    printf 'Package: nowhere\nVersion: 1.0\nArchitecture: all\nDescription: unfetchable\n\n'
} > "$repo/Packages"

# "gone" is in the index, checksum and all, but the file is not there.
rm "$repo/gone_1.0.aeltra"

http_serve "$repo" "$work/http.log" || skip "could not start a local HTTP server"
note "serving $repo on 127.0.0.1:$HTTP_PORT"

new_root_with_repo() {
    rm -rf "$1"
    new_root "$1"
    echo "src testrepo http://127.0.0.1:$HTTP_PORT" >> "$1/etc/aept/aept.conf"
    out=$(aept_run "$1" update 2>&1) || fail "update for $1 failed:
$out"
}

root=$work/root
new_root_with_repo "$root"

# ── a package the index gives no location for ────────────────────────

out=$(aept_run "$root" install --non-interactive nowhere 2>&1)
rc=$?
[ "$rc" -eq 0 ] && fail "installing a package without a Filename reported success:
$out"
echo "$out" | grep -q "no download location" \
    || fail "the failure does not name the missing location:
$out"
note "a stanza without a Filename fails with a clear error"

# ── a package the server no longer has ───────────────────────────────

out=$(aept_run "$root" install --non-interactive gone 2>&1)
rc=$?
[ "$rc" -eq 0 ] && fail "installing a package the server 404s reported success:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^gone ' \
    && fail "a package that could not be downloaded is registered as installed"
[ -e "$root/var/cache/aept/gone_1.0.aeltra" ] \
    && fail "a failed download left a package in the cache"
note "a 404 on the package fails the install and stores nothing"

# ── --keep-going carries the transaction past a failure ──────────────
#
# The bad package's preinst cannot run in a bare root -- there is no
# /bin/sh, so it dies at exec, which is all a failure needs to be.
# Without --keep-going that ends the transaction; with it, the healthy
# package must still arrive whichever order the solver chose.

make_pkg "$work/good_1.0.aeltra" good 1.0
make_pkg "$work/bad_1.0.aeltra" bad 1.0 preinst 1

out=$(aept_run "$root" install --non-interactive --keep-going \
    "$work/good_1.0.aeltra" "$work/bad_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] && fail "--keep-going hid the failure from the exit code:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^good ' \
    || fail "--keep-going did not carry on to the healthy package:
$out"
[ -f "$root/usr/bin/good" ] || fail "the healthy package left no payload"
note "--keep-going installs what it can and still reports the failure"

# ── --no-cache downloads, installs and keeps nothing ─────────────────

new_root_with_repo "$root"

out=$(aept_run "$root" install --non-interactive --no-cache foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "--no-cache install exited $rc:
$out"
[ -f "$root/usr/bin/foo" ] || fail "--no-cache did not install the payload"
aept_run "$root" list --installed 2>/dev/null | grep -q '^foo ' \
    || fail "--no-cache did not register the package"
[ -e "$root/var/cache/aept/foo_1.0.aeltra" ] \
    && fail "--no-cache left the package in the cache"
note "--no-cache installs the package and keeps no copy"

# --download-only wants the file kept, which is exactly what --no-cache
# forbids; the pair is contradictory and --no-cache gives way.
new_root_with_repo "$root"

out=$(aept_run "$root" install --non-interactive --no-cache --download-only foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "--no-cache --download-only exited $rc:
$out"
echo "$out" | grep -q -- "--no-cache ignored" \
    || fail "the contradiction was not called out:
$out"
[ -f "$root/var/cache/aept/foo_1.0.aeltra" ] \
    || fail "--download-only did not leave the package in the cache"
aept_run "$root" list --installed 2>/dev/null | grep -q '^foo ' \
    && fail "--download-only installed the package anyway"
note "--no-cache gives way to --download-only, with a warning"

# ── a tmp_dir on another filesystem ──────────────────────────────────
#
# The control files are unpacked under tmp_dir and renamed into the
# info directory; with tmp_dir on a different filesystem that rename
# fails with EXDEV and the copy fallback has to carry the files over.

shm=/dev/shm
if [ -d "$shm" ] && [ -w "$shm" ] \
    && [ "$(stat -c %d "$shm")" != "$(stat -c %d "$work")" ]; then
    tmpd=$(mktemp -d "$shm/aept-test-XXXXXX") || fail "mktemp on $shm failed"
    trap 'http_stop; rm -rf "$work" "$tmpd"' EXIT

    new_root_with_repo "$root"
    echo "option tmp_dir $tmpd" >> "$root/etc/aept/aept.conf"

    out=$(aept_run "$root" install --non-interactive foo 2>&1)
    rc=$?
    [ "$rc" -eq 0 ] || fail "install with a cross-device tmp_dir exited $rc:
$out"
    [ -f "$root/usr/bin/foo" ] || fail "the payload did not arrive across the device boundary"
    [ -f "$root/var/lib/aept/info/foo.list" ] \
        || fail "the file list did not arrive across the device boundary"
    note "a tmp_dir on another filesystem falls back to copying"
else
    note "$shm is unavailable or on the same filesystem; the EXDEV case is skipped"
fi

exit 0
