#!/bin/sh
# test_ssl_client_cert.sh - the configured TLS client certificate must
# still reach libfetch now that it no longer travels via the
# environment.  test_ssl_env.c covers the other half: that it does not
# reach anything else.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools python3

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
: > "$repo/Packages"

# Not a certificate.  libfetch loads the file while building the SSL
# context, before any handshake, so a file it cannot parse makes it say
# so and name the path — which is exactly the evidence this test wants.
cert=$work/not-a-cert.pem
printf 'definitely not a PEM certificate\n' > "$cert"

http_serve "$repo" "$work/http.log" || skip "could not start a local HTTP server"

root=$work/root
new_root "$root"
{
    echo "option check_signature 0"
    echo "option ssl_client_cert $cert"
    # Addressed as https:// so libfetch builds an SSL context.  The
    # server speaks plain HTTP, but the client certificate is loaded
    # before the handshake, so it never gets that far.
    echo "src testrepo https://127.0.0.1:$HTTP_PORT"
} >> "$root/etc/aept/aept.conf"

out=$(aept_run "$root" update 2>&1)
rc=$?

[ "$rc" -ne 0 ] || fail "update succeeded against a plain-HTTP server on https://:
$out"

# ── the certificate reached libfetch ─────────────────────────────────
#
# Guards against "fixing" the leak by dropping the setting on the floor:
# with nothing configured, libfetch has no certificate to load and says
# nothing about one.  OpenSSL is a hard requirement in configure.ac and
# the complaint is an unconditional fprintf, so its absence means the
# configuration did not arrive — never that this build cannot report it.

printf '%s\n' "$out" | grep -q 'Could not load client certificate' \
    || fail "libfetch never tried to load a client certificate, so the
configured one did not reach it:
$out"

printf '%s\n' "$out" | grep -q "$cert" \
    || fail "libfetch did not name the configured certificate:
$out"
note "the configured certificate was handed to libfetch"

exit 0
