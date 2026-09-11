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

# ── asking for a virtual name counts as asking ───────────────────────
#
# "aept install mta" resolves through Provides, so the package that
# lands is not the name that was typed.  If the match is missed it gets
# marked auto, nothing requires it, and the next autoremove deletes
# what the user just asked for.

mkdir -p "$work/postfix/usr/share/postfix"
printf 'p\n' > "$work/postfix/usr/share/postfix/f"
make_pkg_tree "$work/postfix_1.0.aeltra" postfix 1.0 "Provides: mta" "$work/postfix"
{
    packages_stanza libx 1.0 "$work/libx_1.0.aeltra" "Depends: liby"
    packages_stanza liby 1.0 "$work/liby_1.0.aeltra"
    packages_stanza postfix 1.0 "$work/postfix_1.0.aeltra" "Provides: mta"
} > "$list"
cp "$work/postfix_1.0.aeltra" "$cache/"

out=$(aept_run "$root" install --non-interactive mta 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing by the virtual name mta exited $rc:
$out"
installed postfix || fail "installing mta did not bring in postfix:
$out"
grep -q '^postfix$' "$auto_file" 2>/dev/null \
    && fail "the package that satisfied the requested virtual name was marked auto:
$(cat "$auto_file" 2>/dev/null)"
note "installing a virtual name marks its provider manual, not auto"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove after the virtual install exited $rc:
$out"
installed postfix || fail "autoremove deleted the provider the user asked for"
note "and autoremove leaves it alone afterwards"

# ── two packages needing the same dependency ─────────────────────────
#
# Reachability is walked from every explicitly installed package, so a
# shared dependency is reached twice.  Without the guard that stops the
# second visit the walk recurses forever on a cycle; with it, libx is
# still needed by app2 once app is gone and must survive.

mkdir -p "$work/app2/usr/share/app2"
printf 'a2\n' > "$work/app2/usr/share/app2/f"
make_pkg_tree "$work/app2_1.0.aeltra" app2 1.0 "Depends: libx" "$work/app2"

aept_run "$root" install --non-interactive "$work/app_1.0.aeltra" >/dev/null 2>&1 \
    || fail "reinstalling app failed"
aept_run "$root" install --non-interactive "$work/app2_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing app2 failed"

out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove with a shared dependency exited $rc:
$out"
installed libx || fail "a dependency of two installed packages was removed"

aept_run "$root" remove --non-interactive app >/dev/null 2>&1 \
    || fail "removing app failed"
out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove after removing one dependant exited $rc:
$out"
installed libx || fail "libx was removed while app2 still needs it:
$out"
installed liby || fail "liby was removed while libx still needs it:
$out"
note "a dependency shared by two packages survives losing one of them"

# A Pre-Depends is a dependency too.  libsolv keeps it in the same
# requires list behind a marker, which the reachability walk has to step
# over rather than treat as a package id.
mkdir -p "$work/app3/usr/share/app3"
printf 'a3\n' > "$work/app3/usr/share/app3/f"
make_pkg_tree "$work/app3_1.0.aeltra" app3 1.0 "Pre-Depends: liby" "$work/app3"
aept_run "$root" install --non-interactive "$work/app3_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing app3 failed"
out=$(aept_run "$root" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove with a pre-dependency exited $rc:
$out"
installed liby || fail "a pre-dependency was not counted as needed:
$out"
note "a Pre-Depends counts as needing the package"

# ── autoremove with nothing installed at all ─────────────────────────
empty=$work/empty
new_root "$empty"
out=$(aept_run "$empty" autoremove --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "autoremove on an empty root exited $rc:
$out"
case $out in
    *"nothing to do"*) ;;
    *) fail "autoremove on an empty root said something else:
$out" ;;
esac
note "autoremove on an empty root says so and succeeds"

exit 0
