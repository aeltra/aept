#!/bin/sh
# test_auth_file.sh - source credentials from an auth.conf: which file
# the command line reads, that an unreadable explicit one is an error,
# that plain http gets nothing on the wire, and that no password from
# the file reaches the output.
#
# The positive wire case needs an https server aept trusts, which the
# CLI cannot be pointed at; test_redact_credentials.sh covers the path
# from src->user/src->password to the wire, and test_authfile.c how
# they are filled.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools python3 gzip

# Never a real secret: the test greps its own output for this string.
PASS=s3cretpw

work=$(mktemp -d) || fail "mktemp failed"
trap 'cond_stop; rm -rf "$work"' EXIT

root=$work/root
new_root "$root"

# Nothing listens on port 1: the update fails fast, after the debug
# line naming the file has been logged.
echo "src/gz secure https://127.0.0.1:1/repo" >> "$root/etc/aept/aept.conf"

write_auth() {
    mkdir -p "$(dirname "$1")"
    printf 'machine 127.0.0.1:1 login %s password %s\n' "$2" "$PASS" > "$1"
    chmod 600 "$1"
}

# The file the update says the source's credentials came from.
used_file() {
    aept_run "$root" -v "$@" update 2>&1 \
        | sed -n "s/.*source 'secure' uses credentials from '\(.*\)'.*/\1/p"
}

# ── no file, no credentials ──────────────────────────────────────────

[ -z "$(used_file)" ] || fail "a root without an auth file still used credentials"
note "without an auth file, no credentials are used"

# ── the system file, under the offline root ──────────────────────────

write_auth "$root/etc/aept/auth.conf" system
got=$(used_file)
[ "$got" = "$root/etc/aept/auth.conf" ] \
    || fail "the system file was not used: '$got'"
note "the system file under the offline root is read"

# ── an explicit file, a host path ────────────────────────────────────

write_auth "$work/explicit.conf" explicit
got=$(used_file --auth-file "$work/explicit.conf")
[ "$got" = "$work/explicit.conf" ] \
    || fail "--auth-file was not used: '$got'"
note "--auth-file replaces the system file and is not prefixed"

out=$(aept_run "$root" --auth-file "$work/missing.conf" update 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a missing --auth-file should fail"
echo "$out" | grep -q "cannot open auth file '$work/missing.conf'" \
    || fail "the failure does not name the file:
$out"
note "a missing --auth-file is an error that names the file"

# ── a build-box target reads the RealHome file ───────────────────────

if [ "$(id -u)" -ne 0 ]; then
    home=$(getent passwd "$(id -u)" | cut -d: -f6)
    if [ -n "$home" ]; then
        touch "$root/etc/target"
        write_auth "$root$home/RealHome/.aeltra/auth.conf" home
        got=$(used_file)
        [ "$got" = "$root$home/RealHome/.aeltra/auth.conf" ] \
            || fail "inside a target the RealHome file was not used: '$got'"
        note "inside a target, the RealHome file wins over the system file"

        rm "$root$home/RealHome/.aeltra/auth.conf"
        got=$(used_file)
        [ "$got" = "$root/etc/aept/auth.conf" ] \
            || fail "without a RealHome file the system file was not used: '$got'"
        note "without a RealHome file, the system file is read"
        rm "$root/etc/target"
    fi
else
    note "skipping the build-box target cases: they need a non-root user"
fi

# ── a looser file is warned about ────────────────────────────────────

chmod 644 "$root/etc/aept/auth.conf"
out=$(aept_run "$root" update 2>&1)
echo "$out" | grep -q "auth file '$root/etc/aept/auth.conf' is accessible by other users" \
    || fail "a world-readable auth file was not warned about:
$out"
chmod 600 "$root/etc/aept/auth.conf"
note "an auth file others can read is warned about"

# ── plain http gets nothing on the wire ──────────────────────────────

repo=$work/repo
mkdir -p "$repo"
printf 'Package: foo\nVersion: 1.0\nArchitecture: all\nFilename: foo_1.0.aeltra\n\n' \
    | gzip -c > "$repo/Packages.gz"

log=$work/cond.log
cond_serve "$repo" both "$log" || skip "could not start the local HTTP server"

plain=$work/plain
new_root "$plain"
echo "src/gz plain http://127.0.0.1:$COND_PORT" >> "$plain/etc/aept/aept.conf"
printf 'machine 127.0.0.1:%s login token password %s\n' "$COND_PORT" "$PASS" \
    > "$plain/etc/aept/auth.conf"
chmod 600 "$plain/etc/aept/auth.conf"

out=$(aept_run "$plain" -v update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the plain http update exited $rc:
$out"
grep -q "^AUTH /Packages.gz -\$" "$log" \
    || fail "credentials from the file went out over plain http:
$(grep '^AUTH ' "$log")"
note "an entry for the host is not sent over plain http"

# ── no password from the file in any output ──────────────────────────

all=$(aept_run "$root" -v -v update 2>&1; aept_run "$root" --auth-file "$work/explicit.conf" -v update 2>&1; echo "$out")
case $all in
    *"$PASS"*) fail "a password from an auth file reached the output:
$all" ;;
esac
note "no password from an auth file reaches the output"

exit 0
