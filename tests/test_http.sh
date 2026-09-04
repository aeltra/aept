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
# since .aeltra packages are.
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

# The same hazard without a failure anywhere: a caller that simply stops
# reading.  The response was fine and the connection is still open, but
# the rest of that body is still on it, so reuse means the next request
# is answered by whatever follows — and /abandon is built so that what
# follows reads as a complete, plausible response.  Binary leftovers
# would only produce a protocol error, and a protocol error on a cached
# connection is retried on a fresh one, which hides the whole thing.
#
# Nothing aept does can reach this — aept_download() reads every
# transfer to the end — but a signal the caller declines to resume from
# would, which is why the rule is "ended at the end of its response",
# not "did not fail".
if [ -n "${PARTIALGET:-}" ] && [ -x "$PARTIALGET" ]; then
    timeout 60 "$PARTIALGET" "$base/abandon" 64 "$base/ok" "$work/v" > "$work/verdict" 2>&1 \
        || fail "after an abandoned response the next request failed:
$(cat "$work/verdict")"
    body_is_file "$work/v" 'hello from ok
'
    note "a response abandoned part-read does not put its connection back"
else
    note "partialget is not built, so the abandoned-response case is skipped"
fi

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
# Persistence is HTTP/1.1's default (RFC 9112 9.3), so what the cache
# has to engage on is the *absence* of a "Connection: close", not the
# presence of a "Connection: keep-alive".  libfetch had it the other way
# round: it waited to be told, and nginx and Caddy — which send no
# Connection header at all — were handed a fresh name lookup and a fresh
# handshake for every index and every package.
#
# The six cases below are the whole decision: told keep-alive, told
# close, told nothing over 1.1, told nothing over 1.0, and each token
# once more inside a list, which is where comparing the whole field
# value against one token goes wrong in both directions.
#
# /close-list and /http10 leave the connection open on the server side
# on purpose.  A server that hung up would produce one connection per
# request whatever the client believed, so the count would prove
# nothing; holding it open makes the count report the client's reading
# of the reply and nothing else.

# fetch_thrice <tag> <path> — restart the stub, fetch <path> three times
# through a single context, and set $conns to the number of connections
# the server accepted.  Leaves $base pointing at the restarted stub.
fetch_thrice() {
    http_stub_stop
    http_stub "$work/count-$1" "$work/stub-$1.log" \
        || skip "could not restart the stub"
    base="http://127.0.0.1:$STUB_PORT"

    timeout 60 "$HTTPGET" "$base$2" "$work/a" "$base$2" "$work/b" \
        "$base$2" "$work/c" >/dev/null 2>&1 \
        || fail "$1: the three downloads did not all succeed"

    conns=$(cat "$work/count-$1")
}

fetch_thrice keepalive /ok
[ "$conns" -eq 1 ] \
    || fail "keep-alive: 3 downloads opened $conns connections, expected 1"
note "three downloads over keep-alive share one connection"

fetch_thrice close /close
[ "$conns" -eq 3 ] \
    || fail "close: 3 downloads opened $conns connections, expected 3"
note "a server that declines keep-alive gets one connection per download"

fetch_thrice implicit /implicit-ka
[ "$conns" -eq 1 ] \
    || fail "an HTTP/1.1 reply with no Connection header is still persistent:
3 downloads opened $conns connections, expected 1"
note "HTTP/1.1 persistence is assumed, not waited for"

fetch_thrice kalist /ka-list
[ "$conns" -eq 1 ] \
    || fail "\"Connection: keep-alive, TE\" names keep-alive:
3 downloads opened $conns connections, expected 1"
note "keep-alive is recognised inside a token list"

fetch_thrice closelist /close-list
[ "$conns" -eq 3 ] \
    || fail "\"Connection: TE, close\" names close:
3 downloads opened $conns connections, expected 3"
note "close is recognised inside a token list"

fetch_thrice http10 /http10
[ "$conns" -eq 3 ] \
    || fail "an HTTP/1.0 reply with no Connection header is not persistent:
3 downloads opened $conns connections, expected 3"
note "HTTP/1.0 keeps the opposite default"

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

# ── the request identifies aept, and the environment cannot change it ──
#
# How a package manager identifies itself is part of the request it
# makes.  libfetch inherited three environment variables that let the
# surrounding process rewrite it: $HTTP_USER_AGENT replaced the
# User-Agent outright, $HTTP_REFERER added a Referer aept never wants to
# send, and $FETCH_BIND_ADDRESS chose the local source address.  All
# three are gone; these assertions are what would notice them coming
# back.

HTTP_USER_AGENT="evil/9" HTTP_REFERER="http://evil.example/" \
    timeout 60 "$HTTPGET" "$base/echo-headers" "$out" >/dev/null 2>&1 \
    || fail "user agent: the echo request itself failed"

grep -q '^user-agent: aept/' "$out" \
    || fail "the request did not identify aept:
$(cat "$out")"
grep -q 'evil/9' "$out" \
    && fail "\$HTTP_USER_AGENT rewrote the User-Agent:
$(cat "$out")"
grep -q '^referer: (absent)$' "$out" \
    || fail "\$HTTP_REFERER added a Referer header:
$(cat "$out")"
note "identifies as $(sed -n 's/^user-agent: //p' "$out"), whatever the environment says"

# An address the machine cannot bind to.  While FETCH_BIND_ADDRESS was
# honoured, the bind failed, that address was skipped, and the download
# failed with it; now the variable is simply not consulted.
FETCH_BIND_ADDRESS=192.0.2.1 timeout 60 "$HTTPGET" "$base/ok" "$out" >/dev/null 2>&1 \
    || fail "\$FETCH_BIND_ADDRESS is still honoured: it broke the connection"
body_is_file "$out" 'hello from ok
'
note "\$FETCH_BIND_ADDRESS no longer chooses the local address"

# ── malformed status lines ───────────────────────────────────────────
#
# Everything the reply parser can be handed that is not a status line
# it accepts: a token that is not "HTTP", a version this client does
# not speak, a reply code that is not three digits.  Each is a protocol
# error, not a status to be interpreted.

for path in /badstatus /badversion /badminor /badcode; do
    expect_fail "$path"
    [ -e "$out" ] && fail "$path: a body arrived through a bad status line"
done
note "a malformed status line is a protocol error, whatever is malformed"

# A missing version is the one malformation that is tolerated, because
# servers that old really existed and the parser keeps a branch for
# them.
expect_ok /noversion
body_is /noversion 'versionless ok
'
note "a status line with no HTTP version at all is still accepted"

# ── chunk framing variants ───────────────────────────────────────────

expect_ok /chunkext
body_is /chunkext 'first-'
note "a chunk extension is skipped, not rejected"

expect_ok /chunklf
body_is /chunklf 'first-'
note "a two-digit chunk-size line ended by a bare LF is accepted"

expect_ok /chunkclen
body_is /chunkclen 'first-second-third
'
note "chunked framing governs when Content-Length is also present"

expect_fail /chunkemptyhdr
[ -e "$out" ] && fail "/chunkemptyhdr: a body was accepted through an empty chunk header"
note "an empty line where a chunk size belongs is a protocol error"

expect_fail /chunknotrailer
[ -e "$out" ] && fail "/chunknotrailer: a chunk whose closing CRLF never came was accepted"
note "a stream ending between chunk data and its CRLF fails"

# ── more statuses ────────────────────────────────────────────────────

expect_ok /see303
body_is /see303 'hello from ok
'
note "a 303 is followed like the other redirects"

expect_fail /range416
[ -e "$out" ] && fail "/range416: a body arrived through a 416"
note "a 416 to a request that sent no Range header is refused"

expect_fail /proxy407
note "a 407 is an error, not an invitation to retry"

expect_fail /error500
[ -e "$out" ] && fail "/error500: an error body was kept as a download"
note "a 500 fails and leaves no output file"

# ── Location, in every wrong place ───────────────────────────────────

expect_ok /oklocation
body_is /oklocation 'stayed here
'
note "a Location header on a 200 is ignored, not followed"

expect_ok /twolocations
body_is /twolocations 'hello from ok
'
note "of two Location headers the last wins, and the first does not leak"

expect_ok /movedabs
body_is /movedabs 'hello from ok
'
note "an absolute URL in Location is followed"

expect_ok /movedcreds
body_is /movedcreds 'authorised
'
note "credentials carried by a Location are used for the redirected request"

expect_fail /movedbad
note "a Location that does not parse fails the transfer"

expect_fail /movednoloc
[ -e "$out" ] && fail "/movednoloc: a redirect with no Location produced a body"
note "a redirect without a Location has nowhere to go and fails"

# ── oversized header values ──────────────────────────────────────────

expect_ok /longheader
body_is /longheader 'padded ok
'
note "a 2 KiB header line grows the line buffer instead of breaking it"

expect_ok /bigetag
body_is /bigetag 'tagged ok
'
note "a validator too long to store is dropped without harming the body"

# ── failures of the caller's own making ──────────────────────────────
#
# aept_download() has failure paths of its own before and after the
# network: a URL that does not parse, and a destination that cannot be
# created.

rm -f "$out"
if timeout 60 "$HTTPGET" "http://[" "$out" >/dev/null 2>&1; then
    fail "a URL that does not parse was reported as a success"
fi
[ -e "$out" ] && fail "a file appeared for an unparsable URL"
note "an unparsable URL fails cleanly"

if timeout 60 "$HTTPGET" "$base/ok" "$work/no/such/dir/out" >/dev/null 2>&1; then
    fail "a destination in a missing directory was reported as a success"
fi
note "an uncreatable destination fails cleanly"

# ── the flags aept never passes ──────────────────────────────────────
#
# aept_download() always calls libfetch with no flags, so the verbose
# path and the address-family selectors are reachable only through the
# rawget harness, which passes them explicitly.

if [ -n "${RAWGET:-}" ] && [ -x "$RAWGET" ]; then
    # Verbose is also what makes an HTTP error return its body stream
    # to be drained, and a drained error body ends at the end of its
    # response -- so the connection is reusable.  Without the flag the
    # error turns back before the body and the connection is dropped.
    http_stub_stop
    http_stub "$work/count5" "$work/stub5.log" || skip "could not restart the stub"
    base="http://127.0.0.1:$STUB_PORT"

    verdicts=$(timeout 60 "$RAWGET" v "$base/notfound" "$work/d1" \
        "$base/ok" "$work/d2" 2>/dev/null)
    printf '%s' "$verdicts" | tr '\n' ' ' | grep -q '^fail ok' \
        || fail "verbose: expected the 404 to fail and the follow-up to succeed:
$verdicts"
    body_is_file "$work/d2" 'hello from ok
'
    conns=$(cat "$work/count5")
    [ "$conns" -eq 1 ] \
        || fail "a drained error body should leave the connection reusable:
$conns connections for two requests"
    note "verbose drains an error body and keeps its connection reusable"

    # The 'A' flag forbids following a redirect, so one is an error.
    timeout 60 "$RAWGET" A "$base/moved301" "$work/d3" >/dev/null 2>&1 \
        && fail "the A flag still followed a redirect"
    note "the A flag turns a redirect into a failure"

    # '4' pins the lookup to IPv4, which the loopback address is; '6'
    # pins it to IPv6, which it is not.
    timeout 60 "$RAWGET" 4 "$base/ok" "$work/d4" >/dev/null 2>&1 \
        || fail "the 4 flag broke an IPv4 fetch"
    body_is_file "$work/d4" 'hello from ok
'
    timeout 60 "$RAWGET" 6 "$base/ok" "$work/d5" >/dev/null 2>&1 \
        && fail "the 6 flag resolved an IPv4 literal anyway"
    note "the address-family flags pin the lookup"

    # A body cut off exactly at the harness's 512-byte read: the
    # failure surfaces on a read that has nothing yet to hand over.
    timeout 60 "$RAWGET" - "$base/truncated512" "$work/d6" >/dev/null 2>&1 \
        && fail "a body truncated on a read boundary was accepted"
    [ -e "$work/d6" ] && fail "/truncated512: a partial body was left behind"
    note "a truncation landing exactly on a read boundary still fails"
else
    note "rawget is not built, so the flag cases are skipped"
fi

exit 0
