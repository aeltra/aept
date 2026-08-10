#!/bin/sh
# test_http.sh - characterisation of the HTTP layer as it behaves today
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# This is not a test of what the HTTP layer *should* do.  It is a record
# of what it *does*, written so that the libfetch fork — pruning the
# unused half, merging the files, moving the state into a per-context
# object — can be shown not to have changed any of it.
#
# It drives aept_download() through the httpget harness rather than
# calling libfetch directly, because aept_download() is the boundary
# that must survive the fork unchanged.
#
# The fork is done, so this now doubles as the regression suite for the
# HTTP layer: assertions that once pinned a defect have been flipped to
# the behaviour the fix produces.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3 timeout sha256sum
[ -n "${HTTPGET:-}" ] && [ -x "$HTTPGET" ] || skip "httpget is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stub_stop; rm -rf "$work"' EXIT

http_stub "$work/count" "$work/stub.log" || skip "could not start the stub server"
base="http://127.0.0.1:$STUB_PORT"
note "stub server on 127.0.0.1:$STUB_PORT"

out=$work/out

# get <path> — fetch one path, leaving the body in $out.  Echoes the
# harness verdict ("ok"/"fail"); sets rc.
get() {
    rm -f "$out"
    timeout 60 "$HTTPGET" "$base$1" "$out" >/dev/null 2>&1
    rc=$?
}

expect_ok() {
    get "$1"
    [ "$rc" -eq 124 ] && fail "$1: timed out waiting for the body"
    [ "$rc" -eq 0 ] || fail "$1: expected success, got exit $rc"
}

expect_fail() {
    get "$1"
    [ "$rc" -eq 124 ] && fail "$1: timed out; it must fail, not hang"
    [ "$rc" -ne 0 ] || fail "$1: expected failure, got success"
}

body_is() {
    printf '%s' "$2" > "$work/want"
    cmp -s "$out" "$work/want" \
        || fail "$1: body differs from expectation:
  got:  $(head -c 80 "$out" | tr -d '\0')
  want: $2"
}

# The same, for a download that did not go through get().
body_is_file() {
    printf '%s' "$2" > "$work/want"
    cmp -s "$1" "$work/want" \
        || fail "$1: body differs from expectation:
  got:  $(head -c 80 "$1" | tr -d '\0')
  want: $2"
}

size_is() {
    _got=$(wc -c < "$out")
    [ "$_got" -eq "$2" ] || fail "$1: got $_got bytes, expected $2"
}

# ── bodies come back byte-exact ──────────────────────────────────────

expect_ok /ok
body_is /ok 'hello from ok
'
note "a plain 200 returns its body exactly"

expect_ok /empty
size_is /empty 0
note "a zero-length body yields an empty file"

# A body with NUL and high bytes: the read path must be binary-clean,
# since .aep packages are.
expect_ok /binary
python3 -c "import sys; sys.stdout.buffer.write(bytes([0,1,2,255,10,0,65,66,0,254]))" \
    > "$work/want.bin"
cmp -s "$out" "$work/want.bin" || fail "/binary: body is not binary-clean"
note "a body containing NUL and high bytes survives intact"

# Larger than the 64 KiB read buffer in aept_download().
expect_ok /large
size_is /large 1048576
python3 -c "import sys; sys.stdout.buffer.write(b'0123456789abcdef'*65536)" \
    > "$work/want.large"
cmp -s "$out" "$work/want.large" || fail "/large: 1 MiB body came back altered"
note "a 1 MiB body crosses the read buffer intact"

# ── chunked transfer encoding ────────────────────────────────────────

expect_ok /chunked
body_is /chunked 'first-second-third
'
note "a chunked body is reassembled with the framing removed"

# ── redirects ────────────────────────────────────────────────────────

for path in /moved301 /found302 /temp307; do
    expect_ok "$path"
    body_is "$path" 'hello from ok
'
done
note "301, 302 and 307 are followed to the target body"

expect_fail /loop
note "a redirect to itself terminates in failure rather than looping"

# ── failures ─────────────────────────────────────────────────────────

expect_fail /notfound
[ -e "$out" ] && fail "/notfound: a file was left behind for a failed download"
note "a 404 fails and leaves no output file"

# ── a body that ends early ───────────────────────────────────────────
#
# The server promises 1000 bytes, sends 10, and hangs up.  This used to
# be reported as a successful download of 10 bytes, so a transfer cut
# short was indistinguishable from a complete one at this layer.
# Packages survived it because their SHA256 is checked afterwards, and a
# signed index survived it because the signature would not verify — but
# an unsigned index was accepted truncated.

expect_fail /truncated
[ -e "$out" ] && fail "/truncated: a short body was left on disk"
note "a body shorter than its Content-Length fails, leaving no file"

# The same, from a server that had announced keep-alive.  Whether the
# connection was a candidate for reuse must not change the verdict.
expect_fail /truncated-ka
[ -e "$out" ] && fail "/truncated-ka: a short body was left on disk"
note "a short body fails even when the server had promised keep-alive"

# Chunked framing has its own end-of-body marker, and a stream that
# stops mid-chunk never reaches it.
expect_fail /chunktrunc
[ -e "$out" ] && fail "/chunktrunc: a partial chunk was left on disk"
note "a chunked body cut off mid-chunk fails"

# A chunk with a corrupt frame must fail too.  Here the framing is
# wrong but a valid terminating chunk follows, so a client that shrugs
# at the bad trailer sees a clean six-byte body rather than an error.
expect_fail /chunkbad
[ -e "$out" ] && fail "/chunkbad: a mis-framed chunk was left on disk:
$(wc -c < "$out") bytes"
note "a chunk not followed by CRLF fails"

# ── a failed stream does not poison the connection cache ─────────────
#
# /chunkbad abandons the stream at an unknown point in the protocol
# with the connection still open and further bytes pending on it.  If
# that connection goes back into the cache, the next request reuses it
# and is answered by those leftover bytes — a *successful* download of
# somebody else's body.  Two downloads on one context, so the cache is
# in play.

verdicts=$(timeout 60 "$HTTPGET" "$base/chunkbad" "$work/t" \
    "$base/ok" "$work/u" 2>/dev/null)
printf '%s' "$verdicts" | tr '\n' ' ' | grep -q '^fail ok' \
    || fail "after a failed stream the next download did not recover:
$verdicts"
body_is_file "$work/u" 'hello from ok
'
note "a failed stream's connection is dropped, not reused"

# ── an unsolicited range reply ───────────────────────────────────────
#
# aept never sends a Range header, so 206 can only come from a server
# that decided to send part of a file unasked.  Taking it would mean
# writing 500 bytes of a 1000-byte object and reporting success — the
# truncation case again, dressed as a legitimate status code.  The
# range machinery that used to accept this is gone.

expect_fail /partial206
[ -e "$out" ] && fail "/partial206: a partial body was left on disk:
$(wc -c < "$out") bytes"
note "an unsolicited 206 is refused, not written as a whole file"

# ── no content length at all ─────────────────────────────────────────

expect_ok /noclen
body_is /noclen 'body without a content length
'
note "a close-delimited body is read to the end"

# ── HTTP basic auth ──────────────────────────────────────────────────
#
# Credentials ride in the source URL: aept hands the URL straight to
# libfetch, which answers the 401 with an Authorization header.  Kept
# through the fork, so pinned here.

rm -f "$out"
timeout 60 "$HTTPGET" "http://user:pass@127.0.0.1:$STUB_PORT/auth" "$out" \
    >/dev/null 2>&1 \
    || fail "basic auth: a URL carrying credentials was rejected"
body_is "/auth" 'authorised
'
note "credentials in the source URL satisfy a 401 challenge"

rm -f "$out"
if timeout 60 "$HTTPGET" "$base/auth" "$out" >/dev/null 2>&1; then
    fail "basic auth: a 401 without credentials was treated as success"
fi
[ -e "$out" ] && fail "basic auth: a file was left behind for a 401"
note "a 401 with no credentials to offer fails"

# ── proxy support ────────────────────────────────────────────────────
#
# A proxied request is recognisable server-side because the whole URL
# becomes the request target.  The host below never resolves, so this
# only succeeds if the proxy was really used.

rm -f "$out"
HTTP_PROXY="$base" timeout 60 "$HTTPGET" "http://repo.invalid/ok" "$out" \
    >/dev/null 2>&1 \
    || fail "proxy: HTTP_PROXY was not honoured"
body_is "proxy" 'reached via proxy
'
note "HTTP_PROXY is honoured, and the request goes out proxy-style"

# Credentials for the proxy ride in the proxy URL, exactly as they do
# for an origin server.  This is the *only* way to authenticate to a
# proxy: upstream's $HTTP_PROXY_AUTH was removed, so if this path ever
# breaks there is nothing left to fall back on.

rm -f "$out"
HTTP_PROXY="http://user:pass@127.0.0.1:$STUB_PORT" \
    timeout 60 "$HTTPGET" "http://repo.invalid/ok" "$out" >/dev/null 2>&1 \
    || fail "proxy auth: credentials in the proxy URL were rejected"
body_is "proxy auth" 'reached via authenticated proxy
'
note "credentials in the proxy URL produce a Proxy-Authorization header"

# ── connection reuse ─────────────────────────────────────────────────
#
# The cache only engages when the server says "Connection: keep-alive"
# explicitly — HTTP/1.1's implicit default is not enough for libfetch.
# Both halves are pinned, because the fork moves this cache into the
# per-context object.

: > "$work/count"
http_stub_stop
http_stub "$work/count" "$work/stub2.log" || skip "could not restart the stub"
base="http://127.0.0.1:$STUB_PORT"

timeout 60 "$HTTPGET" "$base/ok" "$work/a" "$base/ok" "$work/b" \
    "$base/ok" "$work/c" >/dev/null 2>&1 \
    || fail "keep-alive: the three downloads did not all succeed"

conns=$(cat "$work/count")
[ "$conns" -eq 1 ] \
    || fail "keep-alive: 3 downloads opened $conns connections, expected 1"
note "three downloads over keep-alive share one connection"

http_stub_stop
http_stub "$work/count3" "$work/stub3.log" || skip "could not restart the stub"
base="http://127.0.0.1:$STUB_PORT"

timeout 60 "$HTTPGET" "$base/close" "$work/a" "$base/close" "$work/b" \
    "$base/close" "$work/c" >/dev/null 2>&1 \
    || fail "close: the three downloads did not all succeed"

conns=$(cat "$work/count3")
[ "$conns" -eq 3 ] \
    || fail "close: 3 downloads opened $conns connections, expected 3"
note "a server that declines keep-alive gets one connection per download"

# ── contexts do not share connections ────────────────────────────────
#
# The cache belongs to the context.  Two contexts fetching the same
# host must each open their own connection: while the cache was global,
# the second context would silently reuse a connection the first had
# opened — and with it the TLS session, so a request meant to present
# one client certificate went out authenticated as another.
#
# Same three URLs as the keep-alive case above, which needed exactly
# one connection; the only difference here is a context per download.

http_stub_stop
http_stub "$work/count4" "$work/stub4.log" || skip "could not restart the stub"
base="http://127.0.0.1:$STUB_PORT"

timeout 60 "$HTTPGET" -s "$base/ok" "$work/a" "$base/ok" "$work/b" \
    "$base/ok" "$work/c" >/dev/null 2>&1 \
    || fail "separate contexts: the three downloads did not all succeed"

conns=$(cat "$work/count4")
[ "$conns" -eq 3 ] \
    || fail "separate contexts: 3 downloads over 3 contexts opened $conns
connections, expected 3 — a connection crossed between contexts"
note "three contexts keep their connections to themselves"

exit 0
