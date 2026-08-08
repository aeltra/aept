# aeptlib.sh - shared helpers for aept integration tests
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Sourced by test_*.sh, never executed directly.  Expects AEPT_BIN to
# point at the built aept wrapper (set from AM_TESTS_ENVIRONMENT).

skip() { echo "SKIP: $*"; exit 77; }
fail() { echo "FAIL: $*"; exit 1; }
note() { echo "# $*"; }

# Skip the test unless every named tool is on PATH.
require_tools() {
    for _t in "$@"; do
        command -v "$_t" >/dev/null 2>&1 || skip "$_t is not available"
    done
}

require_aept() {
    [ -n "${AEPT_BIN:-}" ] && [ -x "$AEPT_BIN" ] || skip "aept is not built"
}

# make_aep <out.aep> <name> <version> [script-name] [script-exit-code]
#
# Builds a minimal .aep: outer ar containing debian-binary,
# control.tar.gz and data.tar.gz.  The payload is a single file at
# usr/bin/<name>.  When a maintainer script name is given, a script
# exiting with the requested code is added to the control archive.
make_aep() {
    _out=$1 _name=$2 _ver=$3 _script=${4:-} _code=${5:-0}

    case $_out in
        /*) ;;
         *) _out=$PWD/$_out ;;
    esac
    rm -f "$_out"

    _d=$(mktemp -d) || fail "mktemp failed"
    mkdir -p "$_d/c" "$_d/d/usr/bin"

    printf 'Package: %s\nVersion: %s\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' \
        "$_name" "$_ver" > "$_d/c/control"
    printf '%s %s\n' "$_name" "$_ver" > "$_d/d/usr/bin/$_name"

    _members=control
    if [ -n "$_script" ]; then
        printf '#!/bin/sh\nexit %s\n' "$_code" > "$_d/c/$_script"
        chmod 755 "$_d/c/$_script"
        _members="$_members $_script"
    fi

    tar czf "$_d/control.tar.gz" -C "$_d/c" $_members || fail "tar control"
    tar czf "$_d/data.tar.gz"    -C "$_d/d" usr        || fail "tar data"
    printf '2.0\n' > "$_d/debian-binary"

    ( cd "$_d" && ar rc "$_out" debian-binary control.tar.gz data.tar.gz ) \
        || fail "ar failed for $_out"

    rm -rf "$_d"
}

# new_root <dir> — create an offline root with a usable aept.conf.
new_root() {
    mkdir -p "$1/etc/aept"
    printf 'arch all\noption check_signature 0\noption ignore_uid 1\n' \
        > "$1/etc/aept/aept.conf"
}

# aept_run <root> <args...> — invoke aept against an offline root.
aept_run() {
    _root=$1
    shift
    "$AEPT_BIN" -o "$_root" -c "$_root/etc/aept/aept.conf" "$@"
}
