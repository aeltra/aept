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

# make_aep_conffile <out.aep> <name> <version> <conffile-path> <content>
#
# Like make_aep, but the payload also carries one configuration file at
# <conffile-path> (an absolute path) listed in the control archive's
# "conffiles".
make_aep_conffile() {
    _out=$1 _name=$2 _ver=$3 _cf=$4 _content=$5

    case $_out in
        /*) ;;
         *) _out=$PWD/$_out ;;
    esac
    rm -f "$_out"

    _d=$(mktemp -d) || fail "mktemp failed"
    mkdir -p "$_d/c" "$_d/d/usr/bin"

    printf 'Package: %s\nVersion: %s\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' \
        "$_name" "$_ver" > "$_d/c/control"
    printf '%s\n' "$_cf" > "$_d/c/conffiles"
    printf '%s %s\n' "$_name" "$_ver" > "$_d/d/usr/bin/$_name"

    mkdir -p "$_d/d/$(dirname "$_cf")"
    printf '%s\n' "$_content" > "$_d/d/$_cf"

    tar czf "$_d/control.tar.gz" -C "$_d/c" control conffiles || fail "tar control"
    tar czf "$_d/data.tar.gz"    -C "$_d/d" .                 || fail "tar data"
    printf '2.0\n' > "$_d/debian-binary"

    ( cd "$_d" && ar rc "$_out" debian-binary control.tar.gz data.tar.gz ) \
        || fail "ar failed for $_out"

    rm -rf "$_d"
}

# make_aep_script <out.aep> <name> <version> <script-name> <body>
#
# Like make_aep, but the named maintainer script carries the given shell
# body instead of a bare exit.
make_aep_script() {
    _out=$1 _name=$2 _ver=$3 _script=$4 _body=$5

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

    printf '#!/bin/sh\n%s\n' "$_body" > "$_d/c/$_script"
    chmod 755 "$_d/c/$_script"

    tar czf "$_d/control.tar.gz" -C "$_d/c" control "$_script" || fail "tar control"
    tar czf "$_d/data.tar.gz"    -C "$_d/d" usr                || fail "tar data"
    printf '2.0\n' > "$_d/debian-binary"

    ( cd "$_d" && ar rc "$_out" debian-binary control.tar.gz data.tar.gz ) \
        || fail "ar failed for $_out"

    rm -rf "$_d"
}

# packages_stanza <name> <version> <aep-file> — emit one Packages entry.
packages_stanza() {
    printf 'Package: %s\nVersion: %s\nArchitecture: all\nFilename: %s\nSize: %s\nSHA256: %s\nDescription: aept test fixture\n\n' \
        "$1" "$2" "$(basename "$3")" "$(wc -c < "$3")" \
        "$(sha256sum "$3" | cut -d' ' -f1)"
}

# add_repo <root> <name> <url-dir> — register a source and create an
# empty package list for it, ready to be filled with packages_stanza.
add_repo() {
    mkdir -p "$1/var/lib/aept/lists"
    : > "$1/var/lib/aept/lists/$2"
    echo "src $2 file://$3" >> "$1/etc/aept/aept.conf"
}

# new_root <dir> — create an offline root with a usable aept.conf.
new_root() {
    mkdir -p "$1/etc/aept"
    printf 'arch all\noption check_signature 0\noption ignore_uid 1\n' \
        > "$1/etc/aept/aept.conf"
}

# ── signed repository fixtures ───────────────────────────────────────

# make_keypair <dir> — generate a usign keypair and a trust store holding
# the public key under the name usign looks it up by (its fingerprint).
# Sets KEY_SECRET and KEY_TRUSTDB.
make_keypair() {
    mkdir -p "$1/trustdb"
    usign -G -p "$1/pub.key" -s "$1/sec.key" >/dev/null 2>&1 \
        || skip "usign could not generate a keypair"
    cp "$1/pub.key" "$1/trustdb/$(usign -F -p "$1/pub.key")"
    KEY_SECRET=$1/sec.key
    KEY_TRUSTDB=$1/trustdb
}

# make_inpackages <index-file> <secret-key> <out.gz>
#
# Wrap an index in the clearsigned envelope the repository indexer
# produces, then gzip it.  The signature covers the index bytes exactly,
# so the index is concatenated verbatim.
make_inpackages() {
    _idx=$1 _key=$2 _out=$3
    _tmp=$(mktemp -d) || fail "mktemp failed"

    usign -S -m "$_idx" -s "$_key" -x "$_tmp/sig" \
        || fail "usign failed to sign $_idx"

    {
        printf '%s\n' '-----BEGIN SIGNIFY SIGNED MESSAGE-----'
        cat "$_idx"
        printf '%s\n' '-----BEGIN SIGNIFY SIGNATURE-----'
        cat "$_tmp/sig"
        printf '%s\n' '-----END SIGNIFY SIGNATURE-----'
    } > "$_tmp/InPackages"

    gzip -c "$_tmp/InPackages" > "$_out"
    rm -rf "$_tmp"
}

# ── a throwaway HTTP server ──────────────────────────────────────────
#
# libfetch is built without file.c, so HTTP is the only transport aept
# can use; serving over loopback keeps the test offline.

# http_serve <dir> <logfile> — sets HTTP_PORT and HTTP_PID.
http_serve() {
    python3 -u -m http.server 0 --bind 127.0.0.1 --directory "$1" \
        > "$2" 2>&1 &
    HTTP_PID=$!

    _tries=0
    while [ "$_tries" -lt 100 ]; do
        HTTP_PORT=$(sed -n 's/.*port \([0-9][0-9]*\).*/\1/p' "$2" | head -1)
        if [ -n "$HTTP_PORT" ]; then
            return 0
        fi
        kill -0 "$HTTP_PID" 2>/dev/null || return 1
        _tries=$((_tries + 1))
        sleep 0.1
    done

    return 1
}

http_stop() {
    [ -n "${HTTP_PID:-}" ] && kill "$HTTP_PID" 2>/dev/null
    HTTP_PID=
}

# http_stub <count-file> <logfile> — start httpstub.py, the server that
# produces the canned replies the HTTP characterisation tests need.
# Sets STUB_PORT and STUB_PID.
http_stub() {
    python3 -u "${srcdir:-.}/httpstub.py" "$1" > "$2" 2>&1 &
    STUB_PID=$!

    _tries=0
    while [ "$_tries" -lt 100 ]; do
        STUB_PORT=$(sed -n 's/^PORT \([0-9][0-9]*\)$/\1/p' "$2" | head -1)
        if [ -n "$STUB_PORT" ]; then
            return 0
        fi
        kill -0 "$STUB_PID" 2>/dev/null || return 1
        _tries=$((_tries + 1))
        sleep 0.1
    done

    return 1
}

http_stub_stop() {
    [ -n "${STUB_PID:-}" ] && kill "$STUB_PID" 2>/dev/null
    STUB_PID=
}

# aept_run <root> <args...> — invoke aept against an offline root.
aept_run() {
    _root=$1
    shift
    "$AEPT_BIN" -o "$_root" -c "$_root/etc/aept/aept.conf" "$@"
}
