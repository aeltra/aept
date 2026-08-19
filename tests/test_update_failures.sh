#!/bin/sh
# test_update_failures.sh - what `aept update` does when the repository
# misbehaves: a missing index, a corrupt one, and the leftovers of a
# source that no longer exists.
#
# The invariant under test is that failure never costs state: an update
# that cannot produce a verified new index leaves the old one -- and
# its signature and validator -- exactly as they were.
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

cat > "$work/Packages" <<PKGEOF
Package: foo
Version: 1.0
Architecture: all
Filename: foo_1.0.aeltra
Description: a package

PKGEOF
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

lists=$root/var/lib/aept/lists
list=$lists/testrepo

# ── a repository with no index at all ────────────────────────────────

mv "$repo/InPackages.gz" "$work/InPackages.gz.good"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] && fail "an update whose index request 404s reported success:
$out"
echo "$out" | grep -q "InPackages" \
    || fail "the failure does not say what could not be fetched:
$out"
[ -e "$list" ] && fail "an index appeared out of a 404"
note "a 404 on the index fails the update and stores nothing"

# ── a first good update, to have state worth defending ───────────────

mv "$work/InPackages.gz.good" "$repo/InPackages.gz"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the good update exited $rc:
$out"
cmp -s "$list" "$work/Packages" || fail "the good update stored a wrong index"
[ -f "$list.sig" ] || fail "the good update left no signature"
note "a good update stores index and signature"

# ── a corrupt replacement does not cost the good index ───────────────

printf 'this is not gzip\n' > "$repo/InPackages.gz"

# Drop the validator so the request goes out unconditional: the server
# is python3's http.server, whose If-Modified-Since check works at
# whole-second granularity and would otherwise answer 304 for a file
# corrupted within the same second it was published.
rm -f "$list.validator"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] && fail "an update fed garbage reported success:
$out"
cmp -s "$list" "$work/Packages" || fail "the garbage update damaged the stored index"
[ -f "$list.sig" ] || fail "the garbage update removed the signature"
note "a corrupt index is refused and the last good one survives"

mv "$work/InPackages.gz.good" "$repo/InPackages.gz" 2>/dev/null \
    || make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"

# ── leftovers of a removed source are pruned ─────────────────────────
#
# The lists directory holds an index, a .sig and a .validator per
# source.  A source removed from the configuration leaves all three
# behind, and only update ever looks at that directory again.

printf 'Package: ghost\n\n' > "$lists/oldrepo"
printf 'stale\n' > "$lists/oldrepo.sig"
printf 'stale\n' > "$lists/oldrepo.validator"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the pruning update exited $rc:
$out"
for stale in oldrepo oldrepo.sig oldrepo.validator; do
    [ -e "$lists/$stale" ] && fail "$stale survived an update it has no source for"
done
[ -f "$list" ] || fail "pruning took the active source's index with it"
[ -f "$list.sig" ] || fail "pruning took the active source's signature with it"
note "an update prunes the leftovers of removed sources, and nothing else"

exit 0
