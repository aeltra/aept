#!/bin/sh
# test_update_error.sh - what an update that lost a source reports.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# An update carries on past a source that fails, so what it returns is
# an aggregate, and no one source's classification describes it.  A
# timeout in particular must not be passed up: it says "momentary, just
# now, try again", and by the time update returns it may have happened
# thirty seconds and a successful source ago.  And which failure came
# last depends on the order the sources are configured in, so the same
# failures would report differently from one config to the next.
#
# So AEPT_ERR_TIMEOUT is reported only by operations that stop at it --
# install, upgrade, remove -- where it always means "this is why we
# stopped".  An update that lost a source reports AEPT_ERR_GENERAL and
# leaves the detail to the log.  The Python bindings turn TIMEOUT into
# AeptTimeout and everything else into AeptError, so this is what
# decides which exception an embedder catches.
#
# The CLI folds the classification into an exit status, so the harness
# tests/updateerr.c prints it instead.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3 gzip
[ -n "${UPDATEERR:-}" ] && [ -x "$UPDATEERR" ] || skip "updateerr is not built"

# From aept.h; the harness prints the enumerator's value.
ERR_NONE=0
ERR_GENERAL=1
ERR_TIMEOUT=2

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"; [ -n "${STALL_PID:-}" ] && kill "$STALL_PID" 2>/dev/null; [ -n "${HTTP_PID:-}" ] && kill "$HTTP_PID" 2>/dev/null' EXIT

# A source that accepts the connection and then says nothing, and one
# that answers.
stall_serve "$work/stall.log" || skip "could not start the stalling listener"

good=$work/good
mkdir -p "$good"
printf 'Package: foo\nVersion: 1.0\nArchitecture: all\nFilename: foo_1.0.aeltra\nDescription: a package\n\n' \
    > "$good/Packages"
gzip -k "$good/Packages"
http_serve "$good" "$work/http.log" || skip "could not start a local HTTP server"

root=$work/root
new_root "$root"
lists=$root/var/lib/aept/lists

run_update() {
    "$UPDATEERR" "$root/etc/aept/aept.conf" 2>"$work/err.log"
}

# ── the stalled source first, the good one after it ──────────────────

cat > "$root/etc/aept/aept.conf" <<CONF
option lists_dir $lists
option cache_dir $root/var/cache/aept
option lock_file $root/var/lib/aept/lock
option check_signature 0
option network_timeout 1
src/gz stalled http://127.0.0.1:$STALL_PORT
src/gz good http://127.0.0.1:$HTTP_PORT
CONF

out=$(run_update)
rc=$?
[ "$rc" -ne 0 ] || fail "an update with a stalled source reported success:
$out"

err=$(printf '%s\n' "$out" | sed -n 's/^returned -1 error \([0-9]*\)$/\1/p')
[ -n "$err" ] || fail "the harness did not report a classification:
$out
$(cat "$work/err.log")"

[ -f "$lists/good" ] || fail "the update stopped at the stalled source instead of carrying on:
$(cat "$work/err.log")"
note "the update carried on past the stalled source and fetched the good one"

[ "$err" -ne "$ERR_TIMEOUT" ] \
    || fail "an update that carried on past a timeout still reported AEPT_ERR_TIMEOUT"
[ "$err" -eq "$ERR_GENERAL" ] \
    || fail "an update that lost a source reported $err, expected AEPT_ERR_GENERAL ($ERR_GENERAL)"
note "and reports the general failure, not the timeout that happened along the way"

# ── the other way round: the good source first ───────────────────────
#
# The classification must not depend on which source came last.

rm -rf "$lists"
cat > "$root/etc/aept/aept.conf" <<CONF
option lists_dir $lists
option cache_dir $root/var/cache/aept
option lock_file $root/var/lib/aept/lock
option check_signature 0
option network_timeout 1
src/gz good http://127.0.0.1:$HTTP_PORT
src/gz stalled http://127.0.0.1:$STALL_PORT
CONF

out=$(run_update)
err=$(printf '%s\n' "$out" | sed -n 's/^returned -1 error \([0-9]*\)$/\1/p')
[ -n "$err" ] || fail "the harness did not report a classification (reversed order):
$out"
[ "$err" -eq "$ERR_GENERAL" ] \
    || fail "with the sources reversed the update reported $err, expected AEPT_ERR_GENERAL"
note "and the same failure reports the same way whichever source came last"

exit 0
