#!/bin/sh
# test_tls_reject.sh - the server side of TLS verification: a
# certificate the client does not trust must end the fetch, classified
# as exactly that.  The trust store is the system's and deliberately
# not configurable, so the testable rejection is the untrusted chain;
# the hostname check needs a *trusted* certificate with the wrong name,
# which no test can mint without the system's keys.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3 openssl
[ -n "${TLSGET:-}" ] && [ -x "$TLSGET" ] || skip "the tlsget harness is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap '[ -n "${TLS_PID:-}" ] && kill "$TLS_PID" 2>/dev/null; http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
printf 'hello\n' > "$repo/file"

# A self-signed certificate for 127.0.0.1 -- valid in every respect
# except that nothing in the system trust store vouches for it.
openssl req -x509 -newkey rsa:2048 -keyout "$work/key.pem" -out "$work/cert.pem" \
    -days 2 -nodes -subj "/CN=127.0.0.1" \
    -addext "subjectAltName=IP:127.0.0.1" >/dev/null 2>&1 \
    || skip "openssl could not create a certificate"

python3 -u "${srcdir:-.}/tlsserver.py" "$repo" "$work/cert.pem" "$work/key.pem" \
    > "$work/tls.log" 2>&1 &
TLS_PID=$!
_tries=0
TLS_PORT=
while [ "$_tries" -lt 100 ]; do
    TLS_PORT=$(sed -n 's/^PORT \([0-9][0-9]*\)$/\1/p' "$work/tls.log" | head -1)
    [ -n "$TLS_PORT" ] && break
    kill -0 "$TLS_PID" 2>/dev/null || skip "the TLS server did not start"
    _tries=$((_tries + 1))
    sleep 0.1
done
[ -n "$TLS_PORT" ] || skip "the TLS server did not report a port"
note "TLS server with a self-signed certificate on 127.0.0.1:$TLS_PORT"

# ── an untrusted chain is refused, and named as untrusted ────────────

out=$("$TLSGET" "https://127.0.0.1:$TLS_PORT/file" 2>/dev/null)
rc=$?
[ "$rc" -eq 1 ] || fail "tlsget against the untrusted server exited $rc:
$out"
[ "$out" = "fail tls-untrusted" ] \
    || fail "the refusal was not classified as an untrusted certificate: '$out'"
[ ! -s "$work/out" ] || fail "bytes were written despite the refused handshake"
note "self-signed server refused, classified tls-untrusted"

# ── https against a plain-HTTP server is a TLS failure too ───────────

http_serve "$repo" "$work/http.log" || skip "could not start a plain HTTP server"

out=$("$TLSGET" "https://127.0.0.1:$HTTP_PORT/file" 2>/dev/null)
rc=$?
[ "$rc" -eq 1 ] || fail "tlsget https-against-plain-http exited $rc:
$out"
case $out in
    "fail tls-"*) ;;
    *) fail "the non-TLS peer was not classified as a TLS failure: '$out'" ;;
esac
note "https to a plain-http port refused, classified as a TLS failure ($out)"

# ── the verbose path narrates the plain-HTTP fetch ───────────────────
#
# libfetch_info() is behind the 'v' flag aept itself never passes; the
# harness passes it so the connect narration runs at least once.

out=$("$TLSGET" -v "http://127.0.0.1:$HTTP_PORT/file" 2>"$work/verbose.log")
rc=$?
[ "$rc" -eq 0 ] || fail "the verbose plain fetch failed ($rc):
$(cat "$work/verbose.log")"
grep -q "connecting to 127.0.0.1:$HTTP_PORT" "$work/verbose.log" \
    || fail "the verbose narration did not appear:
$(cat "$work/verbose.log")"
note "verbose fetch narrates the connection"

exit 0
