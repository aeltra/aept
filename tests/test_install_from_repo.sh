#!/bin/sh
# test_install_from_repo.sh - the path the product exists for: update a
# signed index, then install a package *by name*, so the bytes come from
# the repository and pass through the checksum, not from a local file
# handed to aept ready-made.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools usign python3 gzip ar tar sha256sum
[ -x /usr/bin/usign ] || skip "aept invokes /usr/bin/usign, which is absent"

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
make_keypair "$work"

make_pkg "$repo/foo_1.0.aeltra" foo 1.0

packages_stanza foo 1.0 "$repo/foo_1.0.aeltra" > "$work/Packages"
make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"

http_serve "$repo" "$work/http.log" || skip "could not start a local HTTP server"
note "serving $repo on 127.0.0.1:$HTTP_PORT"

root=$work/root
new_root "$root"
{
    echo "option usign_keydir $KEY_TRUSTDB"
    echo "option check_signature 1"
    echo "src/gz testrepo http://127.0.0.1:$HTTP_PORT"
} >> "$root/etc/aept/aept.conf"

cache=$root/var/cache/aept

# How often the package file has been fetched so far.
pkg_fetches() { grep -c 'foo_1.0.aeltra' "$work/http.log"; }

# ── update, then install by name ─────────────────────────────────────

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

out=$(aept_run "$root" install --non-interactive foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install by name exited $rc:
$out"

[ "$(pkg_fetches)" -eq 1 ] \
    || fail "the package should have been downloaded exactly once:
$(cat "$work/http.log")"
[ -f "$root/usr/bin/foo" ] || fail "the payload did not land in the root"
grep -q '^foo 1.0$' "$root/usr/bin/foo" \
    || fail "the payload content is wrong: $(cat "$root/usr/bin/foo")"
aept_run "$root" list --installed 2>/dev/null | grep -q '^foo ' \
    || fail "foo is not registered as installed"
[ -f "$cache/foo_1.0.aeltra" ] \
    || fail "the downloaded package is not in the cache"
cmp -s "$cache/foo_1.0.aeltra" "$repo/foo_1.0.aeltra" \
    || fail "the cached package differs from what the repository served"
note "update + install by name: downloaded, verified, extracted, registered"

# ── installing again moves nothing ───────────────────────────────────

out=$(aept_run "$root" install --non-interactive foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "reinstalling an installed package exited $rc:
$out"
[ "$(pkg_fetches)" -eq 1 ] \
    || fail "an already-installed package was downloaded again:
$(cat "$work/http.log")"
note "second install: a no-op, nothing re-fetched"

# ── after a removal, the cached copy is trusted via its checksum ─────

out=$(aept_run "$root" remove --non-interactive foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "remove exited $rc:
$out"
[ ! -f "$root/usr/bin/foo" ] || fail "the payload survived its removal"

out=$(aept_run "$root" install --non-interactive foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "reinstall from cache exited $rc:
$out"
[ "$(pkg_fetches)" -eq 1 ] \
    || fail "the cached package was re-downloaded instead of reused:
$(cat "$work/http.log")"
[ -f "$root/usr/bin/foo" ] || fail "reinstall from cache placed no payload"
note "reinstall: served from the cache, verified by checksum, not re-fetched"

# ── a corrupted cache copy is not trusted ────────────────────────────
#
# The cached file fails its checksum, so it must be discarded and the
# package fetched fresh -- the cache is a convenience, never a source
# of truth.

aept_run "$root" remove --non-interactive foo >/dev/null 2>&1 \
    || fail "second remove failed"
printf 'tampered' >> "$cache/foo_1.0.aeltra"

out=$(aept_run "$root" install --non-interactive foo 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install over a corrupted cache exited $rc:
$out"
[ "$(pkg_fetches)" -eq 2 ] \
    || fail "a corrupted cache copy was not re-fetched:
$(cat "$work/http.log")"
cmp -s "$cache/foo_1.0.aeltra" "$repo/foo_1.0.aeltra" \
    || fail "the re-fetched package did not replace the corrupted copy"
[ -f "$root/usr/bin/foo" ] || fail "install over a corrupted cache placed no payload"
note "corrupted cache copy: discarded, re-fetched, installed"

exit 0
