#!/bin/sh
# test_sha256sums.sh - what the file list records, and the shipped digests.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Every file a package installs is recorded with its size, owner, group
# and sha256 -- what is on disk, as written -- so that a later check can
# tell whether it still is.  A package that ships its own sha256sums
# asserts what the packager built: every file written is compared
# against it before going into place, and a package whose list and
# contents disagree is refused whole.  A package without one is
# installed on aept's own digest of what it wrote.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum stat find

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
info=$root/var/lib/aept/info

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }
# list_line <pkg> <path-without-leading-./>
list_line() { grep "^\./$2	" "$info/$1.list"; }

mkdir -p "$work/t/usr/bin" "$work/t/usr/share/t"
printf 'hello\n' > "$work/t/usr/bin/t"
printf 'data data\n' > "$work/t/usr/share/t/data"
: > "$work/t/usr/share/t/empty"
ln -s t "$work/t/usr/bin/t-link"
ln "$work/t/usr/bin/t" "$work/t/usr/bin/t-hard"

# ── what a package without sha256sums records ────────────────────────

make_pkg_tree "$work/plain_1.0.aeltra" plain 1.0 "" "$work/t"
out=$(aept_run "$root" install --non-interactive "$work/plain_1.0.aeltra" 2>&1)
[ $? -eq 0 ] || fail "installing a package without sha256sums failed:
$out"
installed plain || fail "plain is not installed"

line=$(list_line plain usr/bin/t)
[ -n "$line" ] || fail "usr/bin/t is not in the list:
$(cat "$info/plain.list")"
digest=$(sha256sum "$root/usr/bin/t" | cut -d' ' -f1)
size=$(stat -c %s "$root/usr/bin/t")
uid=$(stat -c %u "$root/usr/bin/t")
gid=$(stat -c %g "$root/usr/bin/t")
mode=$(printf '%#o' "0x$(stat -c %f "$root/usr/bin/t")")
[ "$line" = "$(printf './usr/bin/t\t%s\t%s\t%s\t%s\t%s' "$mode" "$uid" "$gid" "$size" "$digest")" ] \
    || fail "the list line does not describe the file on disk:
$line"
note "a regular file is recorded with mode, uid, gid, size and sha256 as on disk"

line=$(list_line plain usr/share/t/empty)
case $line in
    *"	0	e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") ;;
    *) fail "an empty file is not recorded as size 0 with the empty digest:
$line" ;;
esac
note "an empty file records size 0 and the digest of nothing"

line=$(list_line plain usr/bin/t-hard)
case $line in
    *"	$size	$digest") ;;
    *) fail "the hard link does not carry the first name's size and digest:
$line" ;;
esac
note "a hard link records its target's size and digest"

line=$(list_line plain usr/bin/t-link)
case $line in
    *"	-	-	t") ;;
    *) fail "the symlink does not record no size, no digest, and its target:
$line" ;;
esac
line=$(grep "^\./usr/bin/	" "$info/plain.list")
case $line in
    *"	-	-") ;;
    *) fail "the directory does not record no size and no digest:
$line" ;;
esac
note "a symlink records its target; a directory neither size nor digest"

# ── a package with sha256sums installs, and records the shipped digest ─

aept_run "$root" remove --non-interactive plain >/dev/null 2>&1 || fail "removing plain failed"
make_pkg_sums "$work/sums_1.0.aeltra" sums 1.0 "" "$work/t"
out=$(aept_run "$root" install --non-interactive "$work/sums_1.0.aeltra" 2>&1)
[ $? -eq 0 ] || fail "installing a package with a correct sha256sums failed:
$out"
installed sums || fail "sums is not installed"
line=$(list_line sums usr/bin/t)
case $line in
    *"	$digest") ;;
    *) fail "the recorded digest is not the shipped one:
$line" ;;
esac
note "a package with a correct sha256sums installs and records it"

# ── a package whose contents do not match its list is refused ────────
#
# The list is written for the tree, then one file is changed before the
# data archive is built: the digest shipped is the packager's, the
# content is not.

aept_run "$root" remove --non-interactive sums >/dev/null 2>&1 || fail "removing sums failed"
( cd "$work/t" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | xargs -r sha256sum ) \
    > "$work/lying-sums"
cp -a "$work/t" "$work/t2"
printf 'tampered\n' > "$work/t2/usr/share/t/data"
make_pkg_sums "$work/lying_1.0.aeltra" lying 1.0 "" "$work/t2" "$work/lying-sums"

out=$(aept_run "$root" install --non-interactive "$work/lying_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a package whose file does not match its sha256sums was installed:
$out"
printf '%s\n' "$out" | grep -q "usr/share/t/data.*sha256sums" \
    || fail "the refusal does not name the file and the list:
$out"
installed lying && fail "lying is recorded as installed"
[ -e "$root/usr/share/t/data" ] && fail "the mismatching file went into place"
[ -e "$root/usr/share/t/data.aept-new" ] && fail "a .aept-new of the mismatching file was left"
note "a file that does not match the shipped digest never goes into place"

# ── a list that names a file the archive lacks is refused ────────────

{ cat "$work/lying-sums"; printf '%s  usr/share/t/ghost\n' "$digest"; } > "$work/extra-sums"
make_pkg_sums "$work/extra_1.0.aeltra" extra 1.0 "" "$work/t" "$work/extra-sums"
out=$(aept_run "$root" install --non-interactive "$work/extra_1.0.aeltra" 2>&1)
[ $? -ne 0 ] || fail "a package listing a file it does not ship was installed:
$out"
installed extra && fail "extra is recorded as installed"
note "a sha256sums naming a file the archive lacks is refused"

# ── a malformed list is refused before anything is written ───────────
#
# The refusals above stopped at the bad file, and what came before it
# in the archive stayed on disk: there is no file unwind, by decision,
# and a refused package owns nothing, so those are orphans.  Cleared
# here so this case can tell whether it wrote anything at all.

rm -rf "$root/usr"
printf 'this is not a sums file\n' > "$work/bad-sums"
make_pkg_sums "$work/bad_1.0.aeltra" bad 1.0 "" "$work/t" "$work/bad-sums"
out=$(aept_run "$root" install --non-interactive "$work/bad_1.0.aeltra" 2>&1)
[ $? -ne 0 ] || fail "a package with a malformed sha256sums was installed:
$out"
printf '%s\n' "$out" | grep -q "sha256sums cannot be read" \
    || fail "the refusal does not say the list is unreadable:
$out"
[ -e "$root/usr/bin/t" ] && fail "files were written despite the malformed list"
note "a malformed sha256sums refuses the package before any file is written"

# ── the same on an upgrade ───────────────────────────────────────────

make_pkg_sums "$work/sums_1.0b.aeltra" sums 1.0 "" "$work/t"
aept_run "$root" install --non-interactive "$work/sums_1.0b.aeltra" >/dev/null 2>&1 \
    || fail "installing sums 1.0 failed"
cp -a "$work/t" "$work/t3"
printf 'v2\n' > "$work/t3/usr/bin/t"
make_pkg_sums "$work/sums_2.0.aeltra" sums 2.0 "" "$work/t3" "$work/lying-sums"
out=$(aept_run "$root" install --non-interactive "$work/sums_2.0.aeltra" 2>&1)
[ $? -ne 0 ] || fail "an upgrade whose file does not match its sha256sums went through:
$out"
grep -q '^hello$' "$root/usr/bin/t" || fail "the old file was replaced by the refused upgrade"
aept_run "$root" list --installed 2>/dev/null | grep -q '^sums - 1.0 ' \
    || fail "1.0 is no longer the installed version"
note "a mismatching upgrade is refused and the old version stays"

exit 0
