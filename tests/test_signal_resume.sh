#!/bin/sh
# test_signal_resume.sh - a signal that is not a cancellation must not
# cost the transfer
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# test_cancel.sh covers the signal aept is meant to act on.  This is the
# other kind: one the embedding application handles for its own reasons
# -- a window resize, a child of its own exiting -- which says nothing
# about the download and must not end it.  It still interrupts the wait,
# because every wait in libfetch is a poll(2) and poll is never
# restarted, so EINTR reaches the reader either way.  What it must not
# do is condemn the stream: nothing was consumed, the connection is
# intact, and reading again resumes where the transfer stood.
#
# The peer sends the body a piece at a time, so the client is waiting in
# poll for nearly the whole transfer and the signals land where they
# matter.  Both framings are covered: chunked has a second wait per
# piece, for the CRLF that closes the chunk.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3
[ -n "${STALLCLIENT:-}" ] && [ -x "$STALLCLIENT" ] || skip "stallclient is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'dribble_stop; rm -rf "$work"' EXIT

# What dribbleserver.py sends, in twenty pieces.
i=0
while [ "$i" -lt 20 ]; do
    printf 'piece-%02d\n' "$i"
    i=$((i + 1))
done > "$work/expected"

# check <plain|chunked> — download under a signal storm and require the
# body to arrive whole.
check() {
    _mode=$1
    _log=$work/client-$_mode.log
    _srv=$work/server-$_mode.log
    _out=$work/out-$_mode

    dribble_serve "$_mode" "$_srv" || skip "could not start the dribbling server"
    note "$_mode: dribbling body on 127.0.0.1:$DRIBBLE_PORT"

    # Timeout 0: nothing but the signals can end this, so a failure is
    # unambiguously theirs.
    "$STALLCLIENT" -n -t 0 "http://127.0.0.1:$DRIBBLE_PORT/body" "$_out" > "$_log" 2>&1 &
    _pid=$!

    wait_for_line "$_log" '^ready' 50 || {
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_mode: the harness never started"
    }

    # Signal only once the body is under way.  A signal during the
    # request or the reply headers is a different case -- there is no
    # reader there to retry, and that is what lets a signal end a
    # handshake that hangs -- so hitting it here would be testing
    # something else, intermittently.
    wait_for_line "$_srv" '^PIECE 1' 100 || {
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_mode: the server never started sending the body"
    }

    _i=0
    while [ "$_i" -lt 200 ]; do
        grep -q '^returned' "$_log" 2>/dev/null && break
        kill -USR1 "$_pid" 2>/dev/null || break
        sleep 0.05
        _i=$((_i + 1))
    done

    if ! wait_for_line "$_log" '^returned' 100; then
        kill -9 "$_pid" 2>/dev/null
        wait "$_pid" 2>/dev/null
        fail "$_mode: the download never finished"
    fi
    wait "$_pid" 2>/dev/null

    _signals=$(sed -n 's/^signals //p' "$_log")

    # The verdict first, then whether the case was worth anything: a
    # transfer that died on the first signal stops the storm early, and
    # reporting that as "too few signals" would name the symptom.
    grep -q '^returned 0 error 0$' "$_log" \
        || fail "$_mode: the transfer did not survive ${_signals:-0} signals:
$(cat "$_log")"

    [ "${_signals:-0}" -ge 5 ] \
        || fail "$_mode: only ${_signals:-0} signals were delivered, so the case proves nothing"

    cmp -s "$_out" "$work/expected" \
        || fail "$_mode: the body is not what was sent:
$(wc -c < "$_out" 2>/dev/null) bytes, expected $(wc -c < "$work/expected")"

    note "$_mode: ${_signals} signals during the body, transfer intact"
    dribble_stop
}

check plain
check chunked

exit 0
