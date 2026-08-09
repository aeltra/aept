#!/bin/sh
# test_update_size_limit.sh - a repository index that expands without
# bound must be refused rather than decompressed.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools python3 gzip dd

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo" "$work/trustdb"

# A gzip bomb, in the mild sense that matters here: 70 MiB of zeros in
# roughly 70 KiB on the wire.  Comfortably past the 64 MiB ceiling in
# update.c while staying cheap to build — at 200 MiB, generating it cost
# more than everything else in the suite put together.
dd if=/dev/zero bs=1M count=70 2>/dev/null | gzip -9 -c > "$work/bomb.gz" \
    || fail "could not build the oversized index"

bomb_size=$(wc -c < "$work/bomb.gz")
note "oversized index is $bomb_size bytes compressed, 73400320 expanded"

http_serve "$repo" "$work/http.log" || skip "could not start a local HTTP server"

setup_root() {
    rm -rf "$1"
    new_root "$1"
    {
        echo "option usign_keydir $work/trustdb"
        echo "option check_signature ${2:-1}"
        echo "src/gz testrepo http://127.0.0.1:$HTTP_PORT"
    } >> "$1/etc/aept/aept.conf"
}

root=$work/root
lists=$root/var/lib/aept/lists
list=$lists/testrepo

# ── the signed path: slurp_gz() expands into memory ──────────────────
#
# fetch_signed_index() decompresses InPackages.gz into a buffer before
# it can even look for the envelope, so the ceiling has to stop it there
# — nothing later in the pipeline gets a chance to.

cp "$work/bomb.gz" "$repo/InPackages.gz"

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "update accepted an unbounded InPackages.gz:
$out"

printf '%s\n' "$out" | grep -q 'exceeds' \
    || fail "update failed, but not on the size limit:
$out"
note "oversized InPackages.gz refused"

[ -f "$list" ] && fail "an oversized index was left on disk"

# ── the unsigned path: decompress_gz() expands onto disk ─────────────

rm -f "$repo/InPackages.gz"
cp "$work/bomb.gz" "$repo/Packages.gz"

setup_root "$root" 0
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "update accepted an unbounded Packages.gz:
$out"

printf '%s\n' "$out" | grep -q 'exceeds' \
    || fail "update failed, but not on the size limit:
$out"
note "oversized Packages.gz refused"

# A truncated index is worse than none: it parses, so aept would go on
# to solve against a repository that had been silently cut short.
[ -f "$list" ] && fail "a truncated index was left on disk:
$(wc -c < "$list") bytes"
note "no truncated index left behind"

# ── an index of a sane size still works ──────────────────────────────

cat > "$work/Packages" <<'EOF'
Package: foo
Version: 1.0
Architecture: all
Filename: foo_1.0.aeltra
Description: a package

EOF
gzip -c "$work/Packages" > "$repo/Packages.gz"

setup_root "$root" 0
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update of a normal index exited $rc:
$out"
cmp -s "$list" "$work/Packages" \
    || fail "the ceiling disturbed a normal index"
note "an index below the ceiling is unaffected"

exit 0
