#!/bin/sh
# test_redact_credentials.sh - a password in a source url reaches the
# wire and nothing else: not the log output, not the validator record.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools usign python3 gzip
[ -x /usr/bin/usign ] || skip "aept invokes /usr/bin/usign, which is absent"

# Never a real secret: the test greps its own output for this string.
PASS=s3cretpw

work=$(mktemp -d) || fail "mktemp failed"
trap 'cond_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
make_keypair "$work"

cat > "$work/Packages" <<EOF
Package: foo
Version: 1.0
Architecture: all
Filename: foo_1.0.aeltra
Description: a package
EOF
printf '\n' >> "$work/Packages"
make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"

log=$work/cond.log
cond_serve "$repo" both "$log" || skip "could not start the local HTTP server"
note "serving $repo on 127.0.0.1:$COND_PORT"

root=$work/root
new_root "$root"
{
    echo "option usign_keydir $KEY_TRUSTDB"
    echo "option check_signature 1"
    echo "src/gz testrepo http://user:$PASS@127.0.0.1:$COND_PORT"
} >> "$root/etc/aept/aept.conf"

list=$root/var/lib/aept/lists/testrepo
val=$list.validator

# ── a successful update says nothing it should not ───────────────────

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

case $out in
    *"$PASS"*) fail "the password leaked into the update output:
$out" ;;
esac
echo "$out" | grep -q "127.0.0.1:$COND_PORT/InPackages.gz" \
    || fail "redaction should keep the url legible, not hide it:
$out"
note "the update output names the url without its credentials"

[ -f "$val" ] || fail "no validator was recorded next to the index"
case $(cat "$val") in
    *"$PASS"*) fail "the password leaked into the validator record:
$(cat "$val")" ;;
esac
grep -q "^URL: http://127.0.0.1:$COND_PORT/InPackages.gz\$" "$val" \
    || fail "the record does not name the sanitized url:
$(cat "$val")"
note "the validator record carries the url without its credentials"

# ── the credentials still reach the wire ─────────────────────────────
#
# Redaction that quietly stopped sending the password would look
# identical from the outside, so ask the server what it saw.

b64=$(printf 'user:%s' "$PASS" \
    | python3 -c 'import base64,sys; print(base64.b64encode(sys.stdin.buffer.read()).decode())')
grep -q "^AUTH /InPackages.gz Basic $b64\$" "$log" \
    || fail "the request did not carry the configured credentials:
$(grep '^AUTH ' "$log")"
note "the request carried the credentials as basic auth"

# ── the sanitized record still revalidates ───────────────────────────

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the revalidating update exited $rc:
$out"

grep '^REQ ' "$log" | tail -1 | grep -q ' 304 ' \
    || fail "the second request should have been answered 304: $(grep '^REQ ' "$log" | tail -1)"
echo "$out" | grep -q "up to date" \
    || fail "a 304 was not reported as an up-to-date source:
$out"
note "a credentialed source still earns a 304 from its sanitized record"

# ── failure messages are sanitized too ───────────────────────────────

cond_stop

out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] && fail "an update against a stopped server should fail"

case $out in
    *"$PASS"*) fail "the password leaked into the failure output:
$out" ;;
esac
note "a failed download reports the url without its credentials"

exit 0
