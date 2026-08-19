#!/bin/sh
# test_install_rollback.sh - wherever an install fails, the status
# database must tell the truth about what happened: a package that
# never made it is not registered, one that landed but failed to
# configure is registered as exactly that -- and either way the next
# command still works.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar gzip dd

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
provision_shell "$root" || skip "cannot provision a shell into the offline root"
info=$root/var/lib/aept/info

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

# ── failure at preinst: nothing happened ─────────────────────────────

make_pkg "$work/pre_1.0.aeltra" pre 1.0 preinst 1

out=$(aept_run "$root" install --non-interactive "$work/pre_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a failing preinst did not fail the install:
$out"
installed pre && fail "pre is registered despite the preinst failing"
[ ! -f "$root/usr/bin/pre" ] || fail "the payload landed despite the preinst failing"
[ ! -f "$info/pre.list" ] || fail "a .list was written for the failed install"
[ ! -f "$info/pre.control" ] || fail "a .control was written for the failed install"
leftover=$(find "$root/tmp" -maxdepth 1 -name 'aept-*' 2>/dev/null | wc -l)
[ "$leftover" -eq 0 ] || fail "$leftover scratch directories left behind"
note "preinst failure: nothing registered, nothing on disk, nothing left over"

# The same package fixed installs cleanly afterwards.
make_pkg "$work/pre_1.1.aeltra" pre 1.1
aept_run "$root" install --non-interactive "$work/pre_1.1.aeltra" >/dev/null 2>&1 \
    || fail "installing the fixed pre afterwards failed"
installed pre || fail "the fixed pre is not registered"
aept_run "$root" remove --non-interactive pre >/dev/null 2>&1

# ── failure mid-extraction: not registered, and recoverable ──────────
#
# A data.tar.gz cut in half: the gzip header parses, so the failure
# lands mid-stream, after some entries may already have been written.
# Those bytes are unowned garbage -- what must hold is that the status
# database never claims the package, and that a good copy still
# installs over the debris.  The payload is incompressible so the cut
# falls inside the entry data rather than before the first header.

_d=$(mktemp -d)
mkdir -p "$_d/c" "$_d/t/usr/bin" "$_d/t/usr/share/torn"
printf 'Package: torn\nVersion: 1.0\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' \
    > "$_d/c/control"
printf 'first\n' > "$_d/t/usr/bin/torn"
head -c 262144 /dev/urandom > "$_d/t/usr/share/torn/blob"
tar czf "$_d/control.tar.gz" -C "$_d/c" control
tar czf "$_d/data-full.tar.gz" -C "$_d/t" .
_full=$(wc -c < "$_d/data-full.tar.gz")
dd if="$_d/data-full.tar.gz" of="$_d/data.tar.gz" bs=1 count=$((_full / 2)) 2>/dev/null
printf '2.0\n' > "$_d/debian-binary"
( cd "$_d" && ar rc "$work/torn_1.0.aeltra" debian-binary control.tar.gz data.tar.gz )
( cd "$_d" && cp data-full.tar.gz data.tar.gz &&
  ar rc "$work/torn_good_1.0.aeltra" debian-binary control.tar.gz data.tar.gz )
rm -rf "$_d"

out=$(aept_run "$root" install --non-interactive "$work/torn_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a torn data archive did not fail the install:
$out"
# The tear may surface at the clash check's listing pass (an archive
# header error) or at extraction itself, depending on where the cut
# falls; either is a loud refusal.
printf '%s\n' "$out" | grep -qE "failed to extract data archive|Truncated input" \
    || fail "the extraction failure was not reported:
$out"
installed torn && fail "torn is registered despite the failed extraction"
[ ! -f "$info/torn.list" ] || fail "a .list was written for the failed extraction"
[ ! -f "$info/torn.control" ] || fail "a .control was written for the failed extraction"
note "torn extraction: install failed and nothing was registered"

out=$(aept_run "$root" install --non-interactive "$work/torn_good_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the good copy did not install over the debris:
$out"
installed torn || fail "the good copy is not registered"
grep -q 'first' "$root/usr/bin/torn" || fail "the good copy's payload is wrong"
[ -f "$root/usr/share/torn/blob" ] || fail "the good copy's blob did not land"
note "a good copy installs over whatever the torn one left behind"
aept_run "$root" remove --non-interactive torn >/dev/null 2>&1

# ── failure at postinst: registered as unpacked, removable ───────────
#
# The files are on disk and the .list is true, so pretending the
# install did not happen would be the lie; the honest record is the
# package held at "unpacked".  And a package in that state must still
# be removable -- it is the way out.

make_pkg "$work/post_1.0.aeltra" post 1.0 postinst 1

out=$(aept_run "$root" install --non-interactive "$work/post_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a failing postinst did not fail the install:
$out"
printf '%s\n' "$out" | grep -q "postinst failed" \
    || fail "the postinst failure was not reported:
$out"
[ -f "$root/usr/bin/post" ] || fail "the payload is not on disk"
[ -f "$info/post.list" ] || fail "no .list was written"
[ -f "$info/post.control" ] || fail "no .control was written"
grep -q 'unpacked' "$info/post.control" \
    || fail "the status is not 'unpacked':
$(grep -i status "$info/post.control")"
note "postinst failure: on disk, registered as unpacked, exit non-zero"

out=$(aept_run "$root" remove --non-interactive post 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "removing the unpacked package exited $rc:
$out"
installed post && fail "post is still registered after removal"
[ ! -f "$root/usr/bin/post" ] || fail "the unpacked payload survived removal"
[ ! -f "$info/post.control" ] || fail "the unpacked .control survived removal"
note "the unpacked package removes cleanly -- the way out works"

exit 0
