#!/bin/sh
# test_index_freshness.sh - a signed index carries Origin/Date/Valid-Until,
# and aept checks the expiry when it loads the index, not when it fetches it.
#
# Load time is the only placement that catches the client this is meant to
# catch: one whose updates never arrive.  An attacker who drops the request
# leaves a client sitting on a stale index forever, and nothing on the update
# path can see that.
#
# Whether an expired index is refused or merely warned about is the
# deployment's call, via "option check_index_expiry".
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools usign python3 gzip
[ -x /usr/bin/usign ] || skip "aept invokes /usr/bin/usign, which is absent"

work=$(mktemp -d) || fail "mktemp failed"
trap 'http_stop; rm -rf "$work"' EXIT

repo=$work/repo
mkdir -p "$repo"
make_keypair "$work"

# publish <date> <valid-until> — write and sign an index.  A blank <date>
# publishes one with no header stanza at all, the way a repository indexed
# before the stanza existed looks.
publish() {
    if [ -n "$1" ]; then
        {
            printf 'Origin: Aeltra\n'
            printf 'Date: %s\n' "$1"
            printf 'Valid-Until: %s\n' "$2"
            printf '\n'
        } > "$work/Packages"
    else
        : > "$work/Packages"
    fi

    printf 'Package: foo\nVersion: 1.0\nArchitecture: all\nFilename: foo_1.0.aeltra\nDescription: a package\n\n' \
        >> "$work/Packages"

    make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"
}

http_serve "$repo" "$work/http.log" || skip "could not start a local HTTP server"
note "serving $repo on 127.0.0.1:$HTTP_PORT"

root=$work/root
list=$root/var/lib/aept/lists/testrepo

# setup_root <check_index_expiry>
setup_root() {
    rm -rf "$root"
    new_root "$root"
    {
        echo "option usign_keydir $KEY_TRUSTDB"
        echo "option check_signature 1"
        echo "option check_index_expiry $1"
        echo "src/gz testrepo http://127.0.0.1:$HTTP_PORT"
    } >> "$root/etc/aept/aept.conf"
}

# ── the header stanza is accepted and stored ─────────────────────────

publish 2026-01-02T00:00:00Z 2999-01-01T00:00:00Z
setup_root 0
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

head -1 "$list" | grep -q '^Origin: Aeltra' \
    || fail "the stored index does not begin with the header stanza:
$(head -3 "$list")"
aept_run "$root" list 2>/dev/null | grep -q '^foo ' \
    || fail "the package from the index is not listed"
note "index carrying Origin/Date/Valid-Until accepted and parsed"

# An index that is still valid must say nothing at all about expiry.
out=$(aept_run "$root" list 2>&1)
printf '%s\n' "$out" | grep -q 'expired' \
    && fail "a valid index was reported as expired:
$out"
note "a valid index produces no expiry diagnostic"

# ── expiry is noticed at load time, not at update time ───────────────
#
# The index is fetched while still unexpired, so nothing on the update path
# could have flagged it; only loading it later can.

publish 2026-01-02T00:00:00Z 2020-01-01T00:00:00Z

setup_root 0
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update refused an expired index; expiry is a load-time check:
$out"
printf '%s\n' "$out" | grep -q 'expired' \
    && fail "update reported expiry; that belongs at load time:
$out"
note "update itself says nothing about expiry"

# ── warn-only: the index is used anyway ──────────────────────────────

out=$(aept_run "$root" list 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "list failed with check_index_expiry 0:
$out"
printf '%s\n' "$out" | grep -q 'expired' \
    || fail "no expiry warning from list:
$out"
printf '%s\n' "$out" | grep -q '^foo ' \
    || fail "with check_index_expiry 0 the package should still be listed:
$out"
note "check_index_expiry 0: warns, and uses the index anyway"

# ── enforcing: the index is refused ──────────────────────────────────

setup_root 1
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

out=$(aept_run "$root" list 2>&1)
printf '%s\n' "$out" | grep -q 'expired' \
    || fail "no expiry diagnostic from list:
$out"
printf '%s\n' "$out" | grep -q '^foo ' \
    && fail "an expired index was used despite check_index_expiry 1:
$out"
note "check_index_expiry 1: the expired source is not used for listing"

out=$(aept_run "$root" install foo 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "install proceeded from an expired index:
$out"
printf '%s\n' "$out" | grep -q 'expired' \
    || fail "no expiry diagnostic from install:
$out"
note "check_index_expiry 1: install refuses to proceed"

# ── an index with no Valid-Until is never refused ────────────────────
#
# Repositories indexed before the field existed must keep working, even
# where the check is enforced.

publish "" ""
setup_root 1
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update refused an index with no header stanza:
$out"
out=$(aept_run "$root" list 2>&1)
printf '%s\n' "$out" | grep -q '^foo ' \
    || fail "an index predating the header stanza was refused:
$out"
note "an index carrying no Valid-Until is used, even when enforcing"

# ── a package may not pose as the header stanza ──────────────────────
#
# The control format fixes no field order, so a first stanza that is a
# package need not lead with "Package:".  If such a stanza were read as
# repository metadata, a package could carry its own Valid-Until and
# declare the index permanently fresh.  The whole stanza is checked for a
# Package field, not just its first line.

{
    printf 'Source: foo\n'
    printf 'Valid-Until: 2999-01-01T00:00:00Z\n'
    printf 'Package: foo\n'
    printf 'Version: 1.0\nArchitecture: all\nFilename: foo_1.0.aeltra\nDescription: a package\n\n'
} > "$work/Packages"
make_inpackages "$work/Packages" "$KEY_SECRET" "$repo/InPackages.gz"

setup_root 1
out=$(aept_run "$root" update 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "update exited $rc:
$out"

# The forged Valid-Until must simply not be read.  The index has no header,
# so it is treated as carrying no expiry at all and stays usable.
out=$(aept_run "$root" list 2>&1)
printf '%s\n' "$out" | grep -q '^foo ' \
    || fail "an index whose first stanza is a package was refused:
$out"
note "a package stanza is not mistaken for the header, whatever its field order"

# Same shape, but with an expiry in the past: it must still be ignored
# rather than acted on, or a package could refuse the index it sits in.
sed 's/2999-01-01/2020-01-01/' "$work/Packages" > "$work/Packages.past"
make_inpackages "$work/Packages.past" "$KEY_SECRET" "$repo/InPackages.gz"

setup_root 1
aept_run "$root" update >/dev/null 2>&1 || fail "update failed"
out=$(aept_run "$root" list 2>&1)
printf '%s\n' "$out" | grep -q 'expired' \
    && fail "a package's own Valid-Until was read as the index's:
$out"
printf '%s\n' "$out" | grep -q '^foo ' \
    || fail "the index was refused on a package's forged Valid-Until:
$out"
note "a package's Valid-Until cannot expire the index either"

exit 0
