#!/bin/sh
# test_timeout.sh - a stalled transfer must give up on its own
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# No signal is sent anywhere in this test.  That is the point: an
# embedding application -- the Python bindings, say -- must get control
# back from a peer that has gone quiet without resorting to a signal,
# which would act on the whole process rather than on the one call.
#
# Covered here and nowhere else:
#
#   * the body read, which the old dead deadline code would have covered
#     had anything ever set it;
#   * the TLS handshake, which it could not have covered at any setting,
#     because SSL_connect() waited inside OpenSSL's own read(2);
#   * the timeout being per context rather than a process-wide global.
#
# The peer accepts the connection and then says nothing, so the plain
# case stalls waiting for a response and the TLS case stalls waiting for
# a ServerHello.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3
[ -n "${STALLCLIENT:-}" ] && [ -x "$STALLCLIENT" ] || skip "stallclient is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'stall_stop; rm -rf "$work"' EXIT

stall_serve "$work/stall.log" || skip "could not start the stalling listener"
note "stalling listener on 127.0.0.1:$STALL_PORT"

AEPT_ERR_TIMEOUT=2

# check <label> <url> <extra-args...> — the download must return by
# itself, reporting a timeout, without anyone signalling it.
check() {
    _label=$1
    _url=$2
    shift 2
    _log=$work/log

    : > "$_log"
    rm -f "$work/out"
    "$STALLCLIENT" "$@" "$_url" "$work/out" > "$_log" 2>&1 &
    _pid=$!

    wait_for_line "$_log" '^ready' 50 || {
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_label: the harness never started"
    }

    # A 2s timeout, so 15s is a generous ceiling that still fails fast
    # if the wait is unbounded.
    if ! wait_for_line "$_log" '^returned' 150; then
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_label: never returned -- the wait was not bounded"
    fi
    wait "$_pid" 2>/dev/null

    grep -q '^returned 0 ' "$_log" \
        && fail "$_label: reported success on a stalled transfer"

    grep -q "^returned .* error $AEPT_ERR_TIMEOUT\$" "$_log" \
        || fail "$_label: did not report a timeout, got: $(cat "$_log")"

    note "$_label: timed out and returned"
}

check "http body read" "http://127.0.0.1:$STALL_PORT/Packages"  -t 2
check "TLS handshake"  "https://127.0.0.1:$STALL_PORT/Packages" -t 2

# -o creates a second context, with a much longer timeout, *after* the
# one doing the download.  A process-wide setting would be overwritten
# by it and this case would sit here for 300 seconds.
check "per-context setting" "http://127.0.0.1:$STALL_PORT/Packages" -t 2 -o 300

# 0 disables the timeout.  Assert the stall persists, so that a passing
# run of the cases above means the timeout fired rather than the peer
# having closed the connection on its own.
: > "$work/log"
"$STALLCLIENT" -t 0 "http://127.0.0.1:$STALL_PORT/Packages" "$work/out" > "$work/log" 2>&1 &
pid=$!
wait_for_line "$work/log" '^ready' 50 || fail "0: the harness never started"
sleep 5
if grep -q '^returned' "$work/log" 2>/dev/null; then
    wait "$pid" 2>/dev/null
    fail "0 should disable the timeout, but the transfer ended by itself"
fi
kill -9 "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
note "0 disables the timeout: still stalled after five seconds"

exit 0
