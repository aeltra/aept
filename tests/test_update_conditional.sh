#!/bin/sh
# test_update_conditional.sh - aept update revalidates the index instead
# of downloading it again.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools usign python3 gzip
[ -x /usr/bin/usign ] || skip "aept invokes /usr/bin/usign, which is absent"

work=$(mktemp -d) || fail "mktemp failed"
trap 'cond_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
make_keypair "$work"

# publish <version> — rewrite the index the repository serves.
publish() {
    cat > "$work/Packages" <<EOF
Package: foo
Version: $1
Architecture: all
Filename: foo_$1.aeltra
Description: a package
EOF
    printf '\n' >> "$work/Packages"

    make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"
    gzip -c "$work/Packages" > "$repo/Packages.gz"
}

setup_root() {
    rm -rf "$1"
    new_root "$1"
    {
        echo "option usign_keydir $KEY_TRUSTDB"
        echo "option check_signature ${2:-1}"
        echo "src/gz testrepo http://127.0.0.1:$COND_PORT"
    } >> "$1/etc/aept/aept.conf"
}

# What the server was asked, and how it answered, most recently.
last_req() { grep '^REQ ' "$log" | tail -1; }

root=$work/root
list=$root/var/lib/aept/lists/testrepo
val=$list.validator
log=$work/cond.log

publish 1.0

# ── both validators offered ──────────────────────────────────────────

cond_serve "$repo" both "$log" || skip "could not start the local HTTP server"
note "serving $repo on 127.0.0.1:$COND_PORT, offering ETag and Last-Modified"

setup_root "$root"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the first update exited $rc:
$out"

cmp -s "$list" "$work/Packages" || fail "the first update stored a wrong index"
case $(last_req) in
    "REQ /InPackages.gz 200 inm=- ims=-") ;;
    *) fail "the first request should carry no validators: $(last_req)" ;;
esac
note "first update: an unconditional request, 200, index stored"

[ -f "$val" ] || fail "no validator was recorded next to the index"
grep -q "^URL: http://127.0.0.1:$COND_PORT/InPackages.gz\$" "$val" \
    || fail "the record does not name the url it was written for:
$(cat "$val")"
grep -q '^ETag: "' "$val" || fail "no ETag was recorded:
$(cat "$val")"
grep -q '^Last-Modified: ' "$val" || fail "no Last-Modified was recorded:
$(cat "$val")"
note "both validators recorded, against the url they came from"

first_etag=$(sed -n 's/^ETag: //p' "$val")

# ── the second update revalidates ────────────────────────────────────

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the revalidating update exited $rc:
$out"

case $(last_req) in
    "REQ /InPackages.gz 304 inm=$first_etag ims="*) ;;
    *) fail "the second request should have carried both validators: $(last_req)" ;;
esac
echo "$out" | grep -q "up to date" \
    || fail "a 304 was not reported as an up-to-date source:
$out"
cmp -s "$list" "$work/Packages" || fail "the index did not survive the 304"
[ -f "$list.sig" ] || fail "the signature did not survive the 304"
note "second update: conditional request, 304, nothing re-downloaded or re-verified"

# The index is still usable, which is the thing a 304 is claiming.
aept_run "$root" list 2>/dev/null | grep -q '^foo ' \
    || fail "the kept index no longer lists"
note "the kept index still parses and lists"

# ── a republished index is picked up ─────────────────────────────────

publish 2.0

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the update after a republish exited $rc:
$out"

case $(last_req) in
    "REQ /InPackages.gz 200 inm=$first_etag ims="*) ;;
    *) fail "the third request should have revalidated and been refused: $(last_req)" ;;
esac
cmp -s "$list" "$work/Packages" || fail "the republished index was not installed"
aept_run "$root" list 2>/dev/null | grep -q '^foo - 2\.0' \
    || fail "the new version is not listed"
[ "$(sed -n 's/^ETag: //p' "$val")" != "$first_etag" ] \
    || fail "the recorded ETag was not replaced"
note "a changed index still arrives, and the record follows it"

# ── a record written for another url is not used ─────────────────────
#
# The lists directory holds one index per source whatever it was
# fetched as, so the record has to say which document it describes.

sed 's|^URL: .*|URL: http://127.0.0.1:1/Packages.gz|' "$val" > "$val.tmp"
mv "$val.tmp" "$val"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc after the record was rewritten:
$out"
case $(last_req) in
    "REQ /InPackages.gz 200 inm=- ims=-") ;;
    *) fail "a record for another url was sent anyway: $(last_req)" ;;
esac
note "a record naming a different url is ignored, not sent"

cond_stop

# ── one validator is enough, either one ──────────────────────────────

for only in etag lastmod; do
    log=$work/cond-$only.log
    cond_serve "$repo" "$only" "$log" || skip "could not start the local HTTP server"

    setup_root "$root"
    out=$(aept_run "$root" update 2>&1)
    rc=$?
    [ "$rc" -eq 0 ] || fail "the first $only update exited $rc:
$out"

    out=$(aept_run "$root" update 2>&1)
    rc=$?
    [ "$rc" -eq 0 ] || fail "the second $only update exited $rc:
$out"

    case $only in
        etag)
            case $(last_req) in
                'REQ /InPackages.gz 304 inm="'*' ims=-') ;;
                *) fail "an ETag alone did not revalidate: $(last_req)" ;;
            esac
            ;;
        lastmod)
            case $(last_req) in
                "REQ /InPackages.gz 304 inm=- ims="*GMT) ;;
                *) fail "a date alone did not revalidate: $(last_req)" ;;
            esac
            ;;
    esac
    note "$only alone is enough to revalidate"

    cond_stop
done

# ── the unsigned path revalidates too ────────────────────────────────

log=$work/cond-plain.log
cond_serve "$repo" both "$log" || skip "could not start the local HTTP server"

setup_root "$root" 0
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the first unsigned update exited $rc:
$out"
cmp -s "$list" "$work/Packages" || fail "the unsigned path stored a wrong index"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the second unsigned update exited $rc:
$out"
case $(last_req) in
    "REQ /Packages.gz 304 inm="*) ;;
    *) fail "the unsigned path did not revalidate: $(last_req)" ;;
esac
cmp -s "$list" "$work/Packages" || fail "the unsigned index did not survive the 304"
note "check_signature 0 revalidates Packages.gz the same way"

cond_stop

# ── an unsolicited 304 is a protocol error ───────────────────────────
#
# The same rule as an unsolicited 206: a server answering a question
# that was never asked is not evidence of anything.  Taking it at its
# word would mean reporting an index as current on no grounds at all --
# and here there is not even an index to keep.

log=$work/cond-304.log
cond_serve "$repo" always304 "$log" || skip "could not start the local HTTP server"

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an unsolicited 304 was accepted:
$out"
[ -f "$list" ] && fail "an index appeared out of a 304"
case $(last_req) in
    "REQ /InPackages.gz 304 inm=- ims=-") ;;
    *) fail "the request was not the unconditional one: $(last_req)" ;;
esac
note "a 304 answering no conditional request is refused"

# ── a missing kept file forfeits revalidation ────────────────────────
#
# "Unchanged" keeps what is on disk, so a conditional request is only
# honest while everything it would keep is still there.  With the
# signature gone, a 304 would leave the source unverifiable; the next
# request must go out unconditional and fetch the whole object.

log=$work/cond-lost.log
cond_serve "$repo" both "$log" || skip "could not start the local HTTP server"

setup_root "$root"
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the update before the loss exited $rc:
$out"

rm -f "$list.sig"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the update after the loss exited $rc:
$out"
case $(last_req) in
    "REQ /InPackages.gz 200 inm=- ims=-") ;;
    *) fail "with the signature missing the request was still conditional: $(last_req)" ;;
esac
[ -f "$list.sig" ] || fail "the full re-fetch did not restore the signature"
note "a missing signature file makes the next request unconditional"

cond_stop

# ── the uncompressed path revalidates too ────────────────────────────
#
# A bare "src" source fetches "Packages" with no decompression step, so
# its 304 handling is a third code path beside InPackages.gz and
# Packages.gz.

cp "$work/Packages" "$repo/Packages"

log=$work/cond-bare.log
cond_serve "$repo" both "$log" || skip "could not start the local HTTP server"

rm -rf "$root"
new_root "$root"
echo "src testrepo http://127.0.0.1:$COND_PORT" >> "$root/etc/aept/aept.conf"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the first uncompressed update exited $rc:
$out"
cmp -s "$list" "$work/Packages" || fail "the uncompressed path stored a wrong index"

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the second uncompressed update exited $rc:
$out"
case $(last_req) in
    "REQ /Packages 304 inm="*) ;;
    *) fail "the uncompressed path did not revalidate: $(last_req)" ;;
esac
cmp -s "$list" "$work/Packages" || fail "the uncompressed index did not survive the 304"
note "a bare src source revalidates its Packages the same way"

cond_stop

exit 0
