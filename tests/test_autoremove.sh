#!/bin/sh
# test_autoremove.sh - autoremove takes exactly the packages that were
# installed automatically and are no longer reachable from anything the
# user asked for -- and nothing else.  The failure mode that matters is
# the inverse: deleting something the user installed deliberately.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
cache=$root/var/cache/aept
list=$root/var/lib/aept/lists/testrepo
auto_file=$root/var/lib/aept/auto-installed
mkdir -p "$cache"

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

# app (explicit, local) -> libx (repo) -> liby (repo): a two-level
# dependency chain the solver pulls in on its own.
mkdir -p "$work/app/usr/share/app" "$work/libx/usr/share/libx" "$work/liby/usr/share/liby"
printf 'a\n' > "$work/app/usr/share/app/f"
printf 'x\n' > "$work/libx/usr/share/libx/f"
printf 'y\n' > "$work/liby/usr/share/liby/f"
make_pkg_tree "$work/app_1.0.aeltra"  app  1.0 "Depends: libx" "$work/app"
make_pkg_tree "$work/libx_1.0.aeltra" libx 1.0 "Depends: liby" "$work/libx"
make_pkg_tree "$work/liby_1.0.aeltra" liby 1.0 "" "$work/liby"

add_repo "$root" testrepo "$work"
{
    packages_stanza libx 1.0 "$work/libx_1.0.aeltra" "Depends: liby"
    packages_stanza liby 1.0 "$work/liby_1.0.aeltra"
} > "$list"
cp "$work/libx_1.0.aeltra" "$work/liby_1.0.aeltra" "$cache/"

setup() {
    aept_run "$root" install --non-interactive "$work/app_1.0.aeltra" >/dev/null 2>&1 \
        || fail "installing app with its chain failed"
    installed libx || fail "libx was not pulled in"
    installed liby || fail "liby was not pulled in"
    grep -q '^libx$' "$auto_file" || fail "libx is not marked auto"
    grep -q '^liby$' "$auto_file" || fail "liby is not marked auto"
}

setup
note "app installed; libx and liby pulled in and marked auto"

# ── while app needs them, autoremove touches nothing ─────────────────

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove exited $rc:
$out"
printf '%s\n' "$out" | grep -q "nothing to do" \
    || fail "autoremove with everything needed should say so:
$out"
installed libx || fail "libx was removed while app still needs it"
installed liby || fail "liby was removed while libx still needs it"
note "everything reachable from app: nothing to do"

# ── --noaction announces without removing ────────────────────────────

aept_run "$root" remove --non-interactive app >/dev/null 2>&1 \
    || fail "removing app failed"

out=$(aept_run "$root" autoremove --non-interactive --noaction 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove --noaction exited $rc:
$out"
printf '%s\n' "$out" | grep -q "libx" \
    || fail "--noaction did not announce the candidates:
$out"
installed libx || fail "--noaction removed libx"
installed liby || fail "--noaction removed liby"
note "--noaction announces the orphaned chain and removes nothing"

# ── the whole orphaned chain goes in one pass ────────────────────────

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove exited $rc:
$out"
installed libx && fail "libx survived autoremove"
installed liby && fail "liby survived autoremove"
[ ! -f "$root/usr/share/libx/f" ] || fail "libx's payload survived"
[ ! -f "$root/usr/share/liby/f" ] || fail "liby's payload survived"
grep -q '^libx$' "$auto_file" 2>/dev/null && fail "libx still in the auto file"
note "orphaned libx and liby removed together, chain and all"

# ── a package the user asked for by name is never a candidate ────────
#
# liby installed *by name* this time: same package, same repo, but the
# request is explicit, so it is not marked auto and autoremove must
# never consider it -- however unneeded it looks.

out=$(aept_run "$root" install --non-interactive liby 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing liby by name exited $rc:
$out"
grep -q '^liby$' "$auto_file" 2>/dev/null \
    && fail "an explicitly requested package was marked auto"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove exited $rc:
$out"
installed liby || fail "autoremove deleted a package the user asked for"
note "an explicitly installed liby, needed by nothing, is left alone"

exit 0
