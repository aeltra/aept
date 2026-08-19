#!/bin/sh
# test_tls_verify.sh - the verified side of TLS: a certificate the
# client trusts carries the fetch, and a trusted certificate for the
# wrong name is refused by the hostname check.
#
# The system trust store is deliberately not configurable, so no test
# could reach past the handshake -- every certificate a test can mint
# is untrusted, and test_tls_reject.sh ends there.  The tlsget harness
# reaches further through libfetch_set_ca_file(), the seam that exists
# for exactly this file: the test mints its own CA, trusts it for the
# harness only, and signs two server certificates with it -- one for
# 127.0.0.1 and one for a name this server is not.  The second is what
# makes the hostname check testable at all: a *trusted* chain whose
# name is wrong, which X509_check_host() must refuse unconditionally.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3 openssl
[ -n "${TLSGET:-}" ] && [ -x "$TLSGET" ] || skip "the tlsget harness is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'tls_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
printf 'hello over tls\n' > "$repo/file"
# Larger than the harness's 4 KiB read buffer, so the body crosses many
# SSL_read() calls and their poll()-driven retry loop.
python3 -c "import sys; sys.stdout.buffer.write(b'0123456789abcdef' * 65536)" \
    > "$repo/large"

# ── a CA of our own, and two certificates signed by it ───────────────

openssl req -x509 -newkey rsa:2048 -keyout "$work/ca.key" -out "$work/ca.pem" \
    -days 2 -nodes -subj "/CN=aept test CA" \
    -addext "basicConstraints=critical,CA:TRUE" >/dev/null 2>&1 \
    || skip "openssl could not create a CA"

# issue <name> <subject> <san> — key, CSR and CA-signed certificate.
issue() {
    openssl req -newkey rsa:2048 -keyout "$work/$1.key" -out "$work/$1.csr" \
        -nodes -subj "$2" >/dev/null 2>&1 || return 1
    printf 'subjectAltName=%s\n' "$3" > "$work/$1.ext"
    openssl x509 -req -in "$work/$1.csr" -CA "$work/ca.pem" -CAkey "$work/ca.key" \
        -CAcreateserial -days 2 -extfile "$work/$1.ext" \
        -out "$work/$1.pem" >/dev/null 2>&1
}

issue right "/CN=127.0.0.1" "IP:127.0.0.1" \
    || skip "openssl could not issue the matching certificate"
issue wrong "/CN=wrong.example" "DNS:wrong.example" \
    || skip "openssl could not issue the mismatched certificate"

# ── a trusted certificate for the right name carries the fetch ───────

tls_serve "$repo" "$work/right.pem" "$work/right.key" "$work/tls.log" \
    || skip "the TLS server did not start"
note "TLS server with a CA-signed certificate for 127.0.0.1 on port $TLS_PORT"

out=$("$TLSGET" -C "$work/ca.pem" "https://127.0.0.1:$TLS_PORT/file" "$work/got" 2>&1)
[ "$out" = "ok" ] || fail "the trusted fetch did not succeed: '$out'"
cmp -s "$work/got" "$repo/file" || fail "the body did not survive the TLS transport"
note "a chain the harness trusts verifies, and the body arrives intact"

out=$("$TLSGET" -C "$work/ca.pem" "https://127.0.0.1:$TLS_PORT/large" "$work/got.large" 2>&1)
[ "$out" = "ok" ] || fail "the large trusted fetch did not succeed: '$out'"
cmp -s "$work/got.large" "$repo/large" || fail "a 1 MiB body came back altered over TLS"
note "a 1 MiB body crosses the SSL_read loop intact"

# The verbose narration of the verified connection: cipher, subject,
# issuer.  aept never passes the flag; the harness does.
"$TLSGET" -v -C "$work/ca.pem" "https://127.0.0.1:$TLS_PORT/file" \
    > "$work/verdict" 2> "$work/verbose.log"
grep -q '^ok$' "$work/verdict" || fail "the verbose trusted fetch failed:
$(cat "$work/verbose.log")"
grep -q "Certificate subject:" "$work/verbose.log" \
    || fail "the verbose narration did not name the certificate:
$(cat "$work/verbose.log")"
note "verbose narrates the verified connection"

# ── the seam is opt-in: without -C the same server is refused ────────
#
# This is the assertion that the seam cannot leak into production
# behaviour: a context that never called libfetch_set_ca_file() -- and
# nothing in aept can call it -- still verifies against the system
# store, where our CA does not exist.

out=$("$TLSGET" "https://127.0.0.1:$TLS_PORT/file" 2>/dev/null)
[ "$out" = "fail tls-untrusted" ] \
    || fail "without the harness CA the server should be untrusted: '$out'"
note "without -C the same certificate is refused: the seam is opt-in"

tls_stop

# ── a trusted certificate for the wrong name is refused ──────────────
#
# The chain verifies -- our CA really signed it -- so the handshake
# completes and only X509_check_host() stands between this server and
# the client.  This is the one unconditional security check in the
# tree, and the reason the seam exists: no test could reach it before.

tls_serve "$repo" "$work/wrong.pem" "$work/wrong.key" "$work/tls2.log" \
    || skip "the second TLS server did not start"
note "TLS server with a CA-signed certificate for wrong.example on port $TLS_PORT"

out=$("$TLSGET" -C "$work/ca.pem" "https://127.0.0.1:$TLS_PORT/file" "$work/leak" 2>/dev/null)
rc=$?
[ "$rc" -eq 1 ] || fail "tlsget against the wrong-name server exited $rc: '$out'"
[ "$out" = "fail tls-hostname" ] \
    || fail "the refusal was not classified as a hostname mismatch: '$out'"
[ -s "$work/leak" ] && fail "bytes arrived from a server whose name does not match"
note "a trusted chain for the wrong name is refused, classified tls-hostname"

exit 0
