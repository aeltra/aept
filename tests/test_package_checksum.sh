#!/bin/sh
# test_package_checksum.sh - the checksum is the only thing standing
# between a signed index and unsigned package bytes, so every way the
# bytes can disagree with the index must end in a refusal.
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

# Four packages, each wrong in its own way -- the stanzas are computed
# first, over honest bytes, and the served files are then made to
# disagree with what was signed.

make_pkg "$repo/sub_1.0.aeltra"   sub   1.0
make_pkg "$repo/short_1.0.aeltra" short 1.0
make_pkg "$repo/long_1.0.aeltra"  long  1.0
make_pkg "$repo/nosum_1.0.aeltra" nosum 1.0

{
    packages_stanza sub   1.0 "$repo/sub_1.0.aeltra"
    packages_stanza short 1.0 "$repo/short_1.0.aeltra"
    packages_stanza long  1.0 "$repo/long_1.0.aeltra"
    # A stanza with no SHA256 (and no Size): nothing vouches for the
    # bytes, so nothing may install them.
    printf 'Package: nosum\nVersion: 1.0\nArchitecture: all\nFilename: nosum_1.0.aeltra\nDescription: aept test fixture\n\n'
} > "$work/Packages"
make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"

# Now break the served files.  A substituted body, a truncated one, and
# one with bytes appended -- all still downloadable, none matching what
# the index signed.
make_pkg "$repo/sub_1.0.aeltra" sub 2.0
_size=$(wc -c < "$repo/short_1.0.aeltra")
dd if="$repo/short_1.0.aeltra" of="$repo/short.t" bs=1 count=$((_size - 100)) 2>/dev/null
mv "$repo/short.t" "$repo/short_1.0.aeltra"
printf 'trailing garbage' >> "$repo/long_1.0.aeltra"

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

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

# refuse <name> <expected-message> <label>
#
# The install must fail, say why, register nothing, place nothing, and
# -- for a mismatch -- leave no poisoned file in the cache for the next
# run to trust.
refuse() {
    _name=$1 _msg=$2 _label=$3

    out=$(aept_run "$root" install --non-interactive "$_name" 2>&1)
    rc=$?
    [ "$rc" -ne 0 ] || fail "$_label: aept exited 0:
$out"
    printf '%s\n' "$out" | grep -q "$_msg" \
        || fail "$_label: expected '$_msg' in the output:
$out"
    if aept_run "$root" list --installed 2>/dev/null | grep -q "^$_name "; then
        fail "$_label: the package was registered as installed"
    fi
    [ ! -f "$root/usr/bin/$_name" ] \
        || fail "$_label: the payload landed in the root"
    note "$_label: refused"
}

# ── bytes that do not match the signed index ─────────────────────────

refuse sub   'checksum mismatch' "substituted package"
refuse short 'checksum mismatch' "truncated package"
refuse long  'checksum mismatch' "package with appended bytes"

for _p in sub short long; do
    [ ! -f "$cache/${_p}_1.0.aeltra" ] \
        || fail "$_p: the rejected file survived in the cache"
done
note "every rejected file was deleted from the cache"

# ── a stanza with no checksum at all ─────────────────────────────────
#
# Pinned down here as a *refusal*: an index that does not vouch for the
# bytes leaves nothing to verify against, and unverifiable bytes do not
# get installed.  (The fetched file may remain in the cache -- it is
# harmless there, since without a checksum it can never install.)

refuse nosum 'no checksum' "package without a checksum in the index"

exit 0
