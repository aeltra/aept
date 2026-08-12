#!/bin/sh
# test_cancel.sh - a stalled transfer must always be interruptible
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# The network timeout gives an embedding application a way to get
# control back without signalling the whole process; this test covers
# the other half, that a signal still works when there is no timeout at
# all.  The CLI depends on it for Ctrl-C, and it is the only way out for
# a caller that has deliberately set the timeout to 0.
#
# The peer accepts the connection and then says nothing, so the client
# waits for a first byte that never comes.  Each case runs twice: once
# with the handler installed the way aept's own CLI and CPython install
# it, and once with SA_RESTART, which is what glibc's signal(3) gives a
# C embedder by default and what signal.siginterrupt(sig, False) gives a
# Python one.  SA_RESTART makes the kernel restart an interrupted
# read(2), so it catches any wait that is not inside poll(2) -- which is
# how the TLS handshake used to hang.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3
[ -n "${STALLCLIENT:-}" ] && [ -x "$STALLCLIENT" ] || skip "stallclient is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'stall_stop; rm -rf "$work"' EXIT

stall_serve "$work/stall.log" || skip "could not start the stalling listener"
note "stalling listener on 127.0.0.1:$STALL_PORT"

# check <label> <url> [-r] — the download must stall, and SIGINT must
# end it within five seconds.
check() {
    _label=$1
    _url=$2
    _flag=${3:-}
    _log=$work/log

    : > "$_log"
    rm -f "$work/out"
    # Timeout 0: only the signal can end this.
    # shellcheck disable=SC2086 # _flag is one optional word, or none
    "$STALLCLIENT" $_flag -t 0 "$_url" "$work/out" > "$_log" 2>&1 &
    _pid=$!

    wait_for_line "$_log" '^ready' 50 || {
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_label: the harness never started"
    }

    # It must really be stuck, or the case proves nothing.
    sleep 1
    if grep -q '^returned' "$_log" 2>/dev/null; then
        wait "$_pid" 2>/dev/null
        fail "$_label: the download did not stall, so the test is vacuous"
    fi

    kill -INT "$_pid" 2>/dev/null

    if ! wait_for_line "$_log" '^returned' 50; then
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_label: still blocked five seconds after SIGINT"
    fi
    wait "$_pid" 2>/dev/null

    grep -q '^returned 0 ' "$_log" \
        && fail "$_label: reported success on an interrupted transfer"

    note "$_label: interrupted"
}

check "http, plain handler"  "http://127.0.0.1:$STALL_PORT/Packages"
check "http, SA_RESTART"     "http://127.0.0.1:$STALL_PORT/Packages"  -r
check "https, plain handler" "https://127.0.0.1:$STALL_PORT/Packages"
check "https, SA_RESTART"    "https://127.0.0.1:$STALL_PORT/Packages" -r

exit 0
