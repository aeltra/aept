#!/bin/sh
# test_signature_reject.sh - every way a signature can fail to verify
# ends the same: the update is refused and the previously verified
# index is left exactly as it was.  A rejected update that clobbers the
# good index would turn "block the client" into "break the client" --
# a denial of service with extra steps.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools usign python3 gzip
[ -x /usr/bin/usign ] || skip "aept invokes /usr/bin/usign, which is absent"

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
make_keypair "$work"

# A second keypair the client does not trust.
mkdir -p "$work/rogue"
usign -G -p "$work/rogue/pub.key" -s "$work/rogue/sec.key" >/dev/null 2>&1 \
    || skip "usign could not generate a second keypair"

# The index the client accepts first, and a newer one used as the bait
# in every rejection case -- if a rejection ever slips through, the
# version bump makes it visible.
cat > "$work/Packages" <<'EOF'
Package: foo
Version: 1.0
Architecture: all
Filename: foo_1.0.aeltra
Description: a package

EOF
sed 's/1\.0/2.0/g' "$work/Packages" > "$work/Packages.v2"

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

list=$root/var/lib/aept/lists/testrepo

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the initial update exited $rc:
$out"
cp "$list" "$work/good.list"
cp "$list.sig" "$work/good.sig"
note "good index accepted and on disk"

# reject <label> — run an update against whatever InPackages.gz now
# holds; it must fail, and the index already held must survive intact
# and stay usable.
#
# The validator is dropped first so the request is unconditional:
# python3's http.server answers If-Modified-Since with one-second
# granularity, and a bait file written within the same second as the
# good one would earn a 304 -- an honest "keep what you have", when the
# point here is to make aept look at the bait.
reject() {
    rm -f "$list.validator"
    out=$(aept_run "$root" update 2>&1)
    rc=$?
    [ "$rc" -ne 0 ] || fail "$1: the update was accepted:
$out"
    cmp -s "$list" "$work/good.list" \
        || fail "$1: the previously verified index did not survive"
    cmp -s "$list.sig" "$work/good.sig" \
        || fail "$1: the previously verified signature did not survive"
    aept_run "$root" list 2>/dev/null | grep -q '^foo - 1\.0' \
        || fail "$1: the surviving index no longer lists"
    note "$1: refused, old index intact"
}

# ── signed by a key the client does not trust ────────────────────────

make_inpackages "$work/Packages.v2" "$work/rogue/sec.key" "$repo/InPackages.gz"
reject "untrusted key"

# ── the trusted key's fingerprint file is missing ────────────────────
#
# The signature names a fingerprint; usign looks for exactly that file
# in the keydir.  Same honest key, no way to look it up.

make_inpackages "$work/Packages.v2" "$KEY_SECRET" "$repo/InPackages.gz"
_fp=$(usign -F -p "$work/pub.key")
mv "$KEY_TRUSTDB/$_fp" "$work/parked.key"
reject "fingerprint absent from the trust store"
mv "$work/parked.key" "$KEY_TRUSTDB/$_fp"

# ── a signature over different bytes than the index delivered ────────

make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"
# v2 bytes under the signature from the v1 envelope: each half honest,
# the pairing forged.
{
    printf '%s\n' '-----BEGIN SIGNIFY SIGNED MESSAGE-----'
    cat "$work/Packages.v2"
    zcat "$repo/InPackages.gz" | sed -n '/BEGIN SIGNIFY SIGNATURE/,$p'
} | gzip -c > "$repo/InPackages.gz"
reject "signature over different bytes"

# ── a truncated envelope: message begun, signature never arrives ─────

{
    printf '%s\n' '-----BEGIN SIGNIFY SIGNED MESSAGE-----'
    head -3 "$work/Packages.v2"
} | gzip -c > "$repo/InPackages.gz"
reject "truncated envelope"

# ── and after all of it, an honest update still works ────────────────

make_inpackages "$work/Packages.v2" "$KEY_SECRET" "$repo/InPackages.gz"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the honest update after the attacks exited $rc:
$out"
aept_run "$root" list 2>/dev/null | grep -q '^foo - 2\.0' \
    || fail "the honest v2 index was not picked up"
note "an honest update still lands after every rejection"

exit 0
