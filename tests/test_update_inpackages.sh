#!/bin/sh
# test_update_inpackages.sh - aept update fetches the index and its
# signature as one object, and requires it when signatures are checked.
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

# The index that the repository publishes.
cat > "$work/Packages" <<'EOF'
Package: foo
Version: 1.0
Architecture: all
Filename: foo_1.0.aeltra
Description: a package

EOF

make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"
gzip -c "$work/Packages" > "$repo/Packages.gz"
usign -S -m "$work/Packages" -s "$KEY_SECRET" -x "$repo/Packages.sig" \
    || fail "usign failed"

http_serve "$repo" "$work/http.log" || skip "could not start a local HTTP server"
note "serving $repo on 127.0.0.1:$HTTP_PORT"

setup_root() {
    rm -rf "$1"
    new_root "$1"
    {
        echo "option usign_keydir $KEY_TRUSTDB"
        echo "option check_signature ${2:-1}"
        echo "src/gz testrepo http://127.0.0.1:$HTTP_PORT"
    } >> "$1/etc/aept/aept.conf"
}

root=$work/root
list=$root/var/lib/aept/lists/testrepo

# ── the signed single-object path ────────────────────────────────────

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

[ -f "$list" ] || fail "no package list was written"
cmp -s "$list" "$work/Packages" \
    || fail "the stored index does not match what was signed"
[ -f "$list.sig" ] || fail "no signature was written"
note "index and signature recovered from InPackages.gz, byte-exact"

# It must have used the single object, not the detached pair.
grep -q 'InPackages.gz' "$work/http.log" \
    || fail "InPackages.gz was never requested:
$(cat "$work/http.log")"
if grep -q 'Packages.sig' "$work/http.log"; then
    fail "the detached signature was fetched as well:
$(cat "$work/http.log")"
fi
note "fetched one object; Packages.sig was never requested"

# The index must be usable afterwards.
aept_run "$root" list 2>/dev/null | grep -q '^foo ' \
    || fail "the package from the signed index is not listed"
note "the recovered index parses and lists"

# ── a tampered index must be rejected ────────────────────────────────

sed 's/Version: 1.0/Version: 9.9/' "$work/Packages" > "$work/Packages.bad"
{
    printf '%s\n' '-----BEGIN SIGNIFY SIGNED MESSAGE-----'
    cat "$work/Packages.bad"
    printf '%s\n' '-----BEGIN SIGNIFY SIGNATURE-----'
    cat "$repo/Packages.sig"
    printf '%s\n' '-----END SIGNIFY SIGNATURE-----'
} | gzip -c > "$repo/InPackages.gz"

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "update accepted a tampered index:
$out"
[ -f "$list" ] && fail "a rejected index was left on disk"
note "tampered index rejected and not stored"

# ── a malformed envelope must be rejected ────────────────────────────

printf 'not a clearsigned document at all\n' | gzip -c > "$repo/InPackages.gz"

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "update accepted a malformed envelope:
$out"
[ -f "$list" ] && fail "a malformed index was left on disk"
note "malformed envelope rejected"

# ── no fallback to the detached pair ─────────────────────────────────
#
# Packages.gz and Packages.sig are still served.  A signed repository
# that stops publishing InPackages.gz must fail rather than quietly drop
# back to fetching two objects.

rm -f "$repo/InPackages.gz"

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "update fell back to the detached signature:
$out"
[ -f "$list" ] && fail "an index was stored without InPackages.gz"
note "absent InPackages.gz is an error, not a fallback"

# ── unsigned repositories still work ─────────────────────────────────

setup_root "$root" 0
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "unsigned update exited $rc:
$out"
cmp -s "$list" "$work/Packages" \
    || fail "the unsigned path stored a wrong index"
note "check_signature 0 still fetches Packages.gz"

exit 0
