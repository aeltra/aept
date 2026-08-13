#!/bin/sh
# test_threads.sh - several aept contexts driven from several threads
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# aept documents that independent contexts may be used concurrently.
# Nothing tested that until now.  Five threads run at once: two listing
# separate offline roots, so two libsolv pools are sorted at the same
# time; one downloading, exercising the per-context connection cache;
# and two cycling a package through install and remove, which is what
# reaches the solver, archive extraction, the status database, the
# owner index and triggers.
#
# A plain run catches crashes and wrong results.  It does not catch a
# data race that happens to come out right, which is what
# ThreadSanitizer is for; see CLAUDE.md for that command.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_tools python3 ar tar sha256sum
[ -n "${THREADRACE:-}" ] && [ -x "$THREADRACE" ] || skip "threadrace is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stub_stop; rm -rf "$work"' EXIT

# Two roots with distinct package names, so each pool holds different
# strings at the same name ids.
build_root() {
    _root=$1 _prefix=$2
    new_root "$_root"
    mkdir -p "$_root/var/lib/aept/lists"
    : > "$_root/var/lib/aept/lists/testrepo"
    echo "src testrepo http://127.0.0.1:1" >> "$_root/etc/aept/aept.conf"

    # Deliberately not in alphabetical order: the listing has to sort
    # them, which is the operation under test.
    for _n in 7 3 1 5 2; do
        make_pkg "$work/$_prefix$_n.aeltra" "$_prefix$_n" 1.0
        packages_stanza "$_prefix$_n" 1.0 "$work/$_prefix$_n.aeltra" \
            >> "$_root/var/lib/aept/lists/testrepo"
    done
}

build_root "$work/root-a" alpha
build_root "$work/root-b" bravo

# One package per root for the install/remove threads to cycle, so the
# solver, archive extraction, the status database, the owner index and
# triggers all run concurrently too.
make_pkg "$work/cycle-a.aeltra" cycle-a 1.0
make_pkg "$work/cycle-b.aeltra" cycle-b 1.0
note "two roots prepared, five packages each, listed out of order"

http_stub "$work/count" "$work/stub.log" || skip "could not start the stub"

out=$(timeout 300 "$THREADRACE" 10 "$work/root-a" "$work/root-b" \
        "http://127.0.0.1:$STUB_PORT/ok" \
        "$work/cycle-a.aeltra" "$work/cycle-b.aeltra" 2>&1)
rc=$?

[ "$rc" -eq 124 ] && fail "the threaded run timed out — a deadlock?
$out"
[ "$rc" -eq 0 ] || fail "the threaded run reported failures (exit $rc):
$out"

printf '%s\n' "$out" | sed 's/^/# /'
note "10 iterations x (2 listing + 1 downloading + 2 installing contexts) clean"

exit 0
