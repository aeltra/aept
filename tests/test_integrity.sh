#!/bin/sh
# test_integrity.sh - aept verify: what is on disk against the records.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# One kind of damage at a time, each expected to produce exactly the
# one line that names it and an exit status of 1; a clean root, a
# changed conffile and a list without digests are expected to exit 0.
# The lines are the contract a health check would parse, so they are
# compared whole.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum stat

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
info=$root/var/lib/aept/info

# verify <expected-exit> <expected-output-regex-or-empty> [args...]
verify() {
    _want_rc=$1 _want=$2
    shift 2
    _out=$(aept_run "$root" verify "$@" 2>&1)
    _rc=$?
    [ "$_rc" -eq "$_want_rc" ] || fail "verify $* exited $_rc, expected $_want_rc:
$_out"
    if [ -z "$_want" ]; then
        [ -z "$_out" ] || fail "verify $* printed something for a clean root:
$_out"
    else
        printf '%s\n' "$_out" | grep -Eq "$_want" || fail "verify $* did not report '$_want':
$_out"
        [ "$(printf '%s\n' "$_out" | grep -c .)" -eq 1 ] || fail "verify $* reported more than the one line:
$_out"
    fi
}

mkdir -p "$work/t/usr/bin" "$work/t/etc/t"
printf 'hello\n' > "$work/t/usr/bin/tool"
printf 'other\n' > "$work/t/usr/bin/other"
ln -s tool "$work/t/usr/bin/tool-link"
printf 'setting=1\n' > "$work/t/etc/t/t.conf"
make_pkg_sums "$work/t_1.0.aeltra" t 1.0 "" "$work/t"
d=$(mktemp -d); mkdir -p "$d/c"
# The same package with etc/t/t.conf declared a conffile.
printf 'Package: t\nVersion: 1.0\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' > "$d/c/control"
printf '/etc/t/t.conf\n' > "$d/c/conffiles"
( cd "$work/t" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | xargs -r sha256sum ) > "$d/c/sha256sums"
tar czf "$d/control.tar.gz" --owner=0 --group=0 -C "$d/c" control conffiles sha256sums
tar czf "$d/data.tar.gz" --owner=0 --group=0 -C "$work/t" .
printf '2.0\n' > "$d/debian-binary"
( cd "$d" && ar rc "$work/t_1.0.aeltra" debian-binary control.tar.gz data.tar.gz ) || fail "ar failed"
rm -rf "$d"

aept_run "$root" install --non-interactive "$work/t_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing t failed"

# ── a clean root ─────────────────────────────────────────────────────

verify 0 ""
verify 0 "" t
note "a clean root verifies clean, named or not"

# ── one kind of damage at a time ─────────────────────────────────────

restore() {
    aept_run "$root" install --non-interactive --reinstall "$work/t_1.0.aeltra" >/dev/null 2>&1 \
        || fail "reinstalling t failed"
    verify 0 ""
}

printf 'HELLO\n' > "$root/usr/bin/tool"
verify 1 '^digest +/usr/bin/tool +\(t\)  [0-9a-f]{64} -> [0-9a-f]{64}$'
note "same size, other content: digest"
restore

printf 'hi\n' > "$root/usr/bin/tool"
verify 1 '^size +/usr/bin/tool +\(t\)  6 -> 3$'
note "another size: size, and the digest is not reported on top"
restore

orig_mode=$(stat -c %a "$root/usr/bin/tool")
chmod 600 "$root/usr/bin/tool"
verify 1 "^mode +/usr/bin/tool +\\(t\\)  0$orig_mode -> 0600\$"
note "other permission bits: mode"
restore

rm "$root/usr/bin/tool"
verify 1 '^missing +/usr/bin/tool +\(t\)  was regular file$'
note "gone: missing, saying what was there"
restore

rm "$root/usr/bin/tool"; mkdir "$root/usr/bin/tool"
verify 1 '^type +/usr/bin/tool +\(t\)  regular file -> directory$'
note "something else at the path: type"
rm -rf "$root/usr/bin/tool"; restore

rm "$root/usr/bin/tool-link"; ln -s other "$root/usr/bin/tool-link"
verify 1 '^link +/usr/bin/tool-link +\(t\)  tool -> other$'
note "a symlink pointing elsewhere: link"
restore

# The owner column: written by hand, since the suite cannot chown.
# With ignore_ownership set (as new_root does) it is not checked; with
# the option off it is; and the CLI flag turns it off for one call.
sed -i 's|^\(\./usr/bin/tool\t[0-9]*\t\)[0-9-]*\t[0-9-]*|\112345\t12345|' "$info/t.list"
grep -q "^\./usr/bin/tool	[0-9]*	12345	12345" "$info/t.list" || fail "could not edit the owner column"
verify 0 ""
sed -i '/^option ignore_ownership 1$/d' "$root/etc/aept/aept.conf"
verify 1 '^owner +/usr/bin/tool +\(t\)  12345:12345 -> [0-9]+:[0-9]+$'
verify 0 "" --ignore-ownership
echo 'option ignore_ownership 1' >> "$root/etc/aept/aept.conf"
note "owner is checked only without ignore_ownership, and --ignore-ownership skips it"
restore

# ── what is reported but is not damage ───────────────────────────────

printf 'setting=2\n' > "$root/etc/t/t.conf"
verify 0 '^conffile +/etc/t/t.conf +\(t\)$'
note "a changed conffile is reported, and the exit status is 0"
# A reinstall keeps the admin's edit, as it should; put the original back.
printf 'setting=1\n' > "$root/etc/t/t.conf"
restore

# A list written before digests were recorded: the file cannot be
# checked, and says so, without failing.
sed -i 's|^\(\./usr/bin/tool\t[0-9]*\)\t.*|\1|' "$info/t.list"
verify 0 '^unverifiable +/usr/bin/tool +\(t\)$'
note "a file with no recorded digest is unverifiable, not damaged"
restore

# ── errors ───────────────────────────────────────────────────────────

out=$(aept_run "$root" verify nosuch 2>&1)
rc=$?
[ "$rc" -eq 2 ] || fail "verifying a package that is not installed exited $rc, expected 2:
$out"
printf '%s\n' "$out" | grep -q "not installed" || fail "the error does not say so:
$out"
note "a package that is not installed is an error, exit 2"

exit 0
