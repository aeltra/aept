#!/bin/sh
# test_hardlink_escape.sh - a hardlink target that climbs out of the
# extraction directory must abort the install, not be quietly skipped.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# The hardlink target is the one path in an archive that never passes
# aept_archive_path_is_safe(): the containment check in safe_join() is
# all that stands between it and the rest of the filesystem.  While that
# check answered "skip this entry" and "this entry escapes" with the
# same value, extraction stepped over the second as readily as the
# first and aept reported the package installed.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar python3

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

# make_pkg_hardlink <out.aeltra> <name> <link-target>
#
# A package whose payload is a regular file at usr/bin/<name> plus a
# hardlink at usr/bin/<name>-link pointing at <link-target>.  tar(1)
# cannot be talked into an arbitrary link target, so the data archive is
# written by hand.
make_pkg_hardlink() {
    _out=$1 _name=$2 _target=$3

    case $_out in
        /*) ;;
         *) _out=$PWD/$_out ;;
    esac
    rm -f "$_out"

    _d=$(mktemp -d) || fail "mktemp failed"
    mkdir -p "$_d/c"

    printf 'Package: %s\nVersion: 1.0\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' \
        "$_name" > "$_d/c/control"
    tar czf "$_d/control.tar.gz" -C "$_d/c" control || fail "tar control"

    NAME=$_name TARGET=$_target OUT=$_d/data.tar.gz python3 -c '
import io, os, tarfile

name, target, out = os.environ["NAME"], os.environ["TARGET"], os.environ["OUT"]

with tarfile.open(out, "w:gz") as tf:
    for d in ("usr", "usr/bin"):
        ti = tarfile.TarInfo(d)
        ti.type, ti.mode = tarfile.DIRTYPE, 0o755
        tf.addfile(ti)

    payload = ("%s 1.0\n" % name).encode()
    ti = tarfile.TarInfo("usr/bin/%s" % name)
    ti.size, ti.mode = len(payload), 0o644
    tf.addfile(ti, io.BytesIO(payload))

    ti = tarfile.TarInfo("usr/bin/%s-link" % name)
    ti.type, ti.linkname, ti.mode = tarfile.LNKTYPE, target, 0o644
    tf.addfile(ti)
' || fail "python3 could not build the data archive"

    printf '2.0\n' > "$_d/debian-binary"
    ( cd "$_d" && ar rc "$_out" debian-binary control.tar.gz data.tar.gz ) \
        || fail "ar failed for $_out"

    rm -rf "$_d"
}

# ── a hardlink inside the package is ordinary and must still work ────
#
# The control case.  Refusing every hardlink would pass the assertions
# below while breaking real packages.

root=$work/root-ok
new_root "$root"
make_pkg_hardlink "$work/inside.aeltra" inside usr/bin/inside

out=$(aept_run "$root" install --non-interactive "$work/inside.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "contained hardlink was refused (exit $rc):
$out"
[ -f "$root/usr/bin/inside-link" ] \
    || fail "contained hardlink was not extracted"
note "contained hardlink: installs normally"

# ── a hardlink target that leaves the root must abort the install ────

root=$work/root-escape
new_root "$root"
make_pkg_hardlink "$work/escape.aeltra" escape ../../../../../../etc/passwd

out=$(aept_run "$root" install --non-interactive "$work/escape.aeltra" 2>&1)
rc=$?

[ "$rc" -ne 0 ] \
    || fail "aept exited 0 on a hardlink escaping the extraction directory:
$out"

printf '%s\n' "$out" | grep -q 'escapes extraction directory' \
    || fail "the escape was not reported:
$out"

if aept_run "$root" list --installed 2>/dev/null | grep -q '^escape '; then
    fail "package was registered as installed despite the escaping hardlink"
fi

[ -e "$root/usr/bin/escape-link" ] \
    && fail "the escaping hardlink was created"

note "escaping hardlink: install aborted and nothing registered"
exit 0
