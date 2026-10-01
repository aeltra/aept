#!/bin/sh
# test_download_errors.sh - a failed download says why: the HTTP status
# in words, and for a 401 whether credentials were missing or rejected,
# with a hint when an http source is why none were sent.  Nothing in
# the reasons may quote a password.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools python3

# Never a real secret: the test greps its own output for this string.
PASS=s3cretpw

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stub_stop; rm -rf "$work"' EXIT

http_stub "$work/connections" "$work/stub.log" || skip "could not start the HTTP stub"
base=http://127.0.0.1:$STUB_PORT

# The error a single source's update reports.
update_error() {
    _root=$work/root-$1
    new_root "$_root"
    echo "src/gz s $2" >> "$_root/etc/aept/aept.conf"
    aept_run "$_root" update 2>&1 | grep "failed to download"
}

out=$(update_error missing "$base/auth401")
echo "$out" | grep -q "the server requires credentials (HTTP 401) and none are set; credentials from an auth file are only sent to https sources" \
    || fail "a 401 without credentials is not explained:
$out"
note "a 401 without credentials says none are set, and why for an http source"

out=$(update_error rejected "http://user:$PASS@127.0.0.1:$STUB_PORT/auth401")
echo "$out" | grep -q "the server rejected the credentials (HTTP 401)" \
    || fail "a 401 with credentials is not explained:
$out"
case $out in
    *"$PASS"*) fail "the password leaked into the error:
$out" ;;
esac
note "a 401 with credentials says they were rejected, without quoting them"

out=$(update_error forbidden "$base/status/403")
echo "$out" | grep -q "access denied (HTTP 403)" || fail "a 403 is not explained:
$out"
note "a 403 says access was denied"

out=$(update_error notfound "$base/status/404")
echo "$out" | grep -q "not found (HTTP 404)" || fail "a 404 is not explained:
$out"
note "a 404 says not found"

out=$(update_error servererror "$base/status/500")
echo "$out" | grep -q "'$base/status/500/Packages.gz': HTTP 500\$" \
    || fail "another status is not named:
$out"
note "any other status is named by number"

out=$(update_error refused "http://127.0.0.1:1/repo")
echo "$out" | grep -q "'http://127.0.0.1:1/repo/Packages.gz': ." \
    || fail "a refused connection gives no reason:
$out"
note "a failed connection gives the system's reason"

exit 0
