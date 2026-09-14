#!/bin/sh
# test_protected.sh - the protected mark.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# An installed package carries one of three marks.  auto and manual say
# whether autoremove may take it; protected says nothing may: not a
# removal by name, not a conflict the solver would resolve by removing
# it, not an upgrade that would have to drop it.  The mark is a solver
# job, so every path through the solver honours it the same way, and a
# removal typed by name is refused in words before the solver sees it.
#
# The mark is on a package, not on what it provides.  "Keep a provider
# of init" is a dependency, and belongs in a package that depends on
# init -- protect that package.  The last section shows that shape.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
cache=$root/var/cache/aept
mkdir -p "$cache"

marks_file=$root/var/lib/aept/marks

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }
in_auto() { grep -qx "$1 auto" "$marks_file" 2>/dev/null; }
in_prot() { grep -qx "$1 protected" "$marks_file" 2>/dev/null; }
lines_for() { grep -c "^$1 " "$marks_file" 2>/dev/null; }

# lib is a dependency; app depends on it.  A second lib version exists
# for the upgrade sections, and rival conflicts with lib.
# Relationships go into the control file too (make_pkg_tree), since an
# installed package's are read from there, not from the index.
tree() { mkdir -p "$work/t-$1/usr/bin"; printf '%s\n' "$1" > "$work/t-$1/usr/bin/$1"; }
tree lib; tree app; tree rival; tree lone; tree base; tree init-a; tree init-b
make_pkg "$work/lib_1.0.aeltra" lib 1.0
make_pkg "$work/lib_2.0.aeltra" lib 2.0
make_pkg_tree "$work/app_1.0.aeltra" app 1.0 "Depends: lib" "$work/t-app"
make_pkg_tree "$work/rival_1.0.aeltra" rival 1.0 "Conflicts: lib" "$work/t-rival"
make_pkg "$work/lone_1.0.aeltra" lone 1.0

add_repo "$root" testrepo "$work"
{
    packages_stanza lib 1.0 "$work/lib_1.0.aeltra"
    packages_stanza lib 2.0 "$work/lib_2.0.aeltra"
    packages_stanza app 1.0 "$work/app_1.0.aeltra" "Depends: lib"
    packages_stanza rival 1.0 "$work/rival_1.0.aeltra" "Conflicts: lib"
    packages_stanza lone 1.0 "$work/lone_1.0.aeltra"
} > "$root/var/lib/aept/lists/testrepo"
cp "$work"/*.aeltra "$cache/"

# ── the mark moves between three exclusive states ────────────────────

aept_run "$root" install --non-interactive lone >/dev/null 2>&1 \
    || fail "installing lone failed"
in_auto lone && fail "an explicitly installed package is marked auto"

aept_run "$root" mark protected lone >/dev/null 2>&1 || fail "mark protected failed"
in_prot lone || fail "mark protected did not record lone"
note "mark protected records the name"

aept_run "$root" mark auto lone >/dev/null 2>&1 || fail "mark auto failed"
in_auto lone || fail "mark auto did not record lone"
in_prot lone && fail "a package marked auto is still protected"

aept_run "$root" mark protected lone >/dev/null 2>&1 || fail "mark protected failed"
in_prot lone || fail "mark protected did not record lone"
in_auto lone && fail "a package marked protected is still auto"
[ "$(lines_for lone)" = 1 ] || fail "lone has $(lines_for lone) lines in the marks file"
note "auto and protected are exclusive, in both directions: one line per name"

aept_run "$root" mark manual lone >/dev/null 2>&1 || fail "mark manual failed"
in_prot lone && fail "mark manual left lone protected"
in_auto lone && fail "mark manual left lone auto"
note "mark manual clears either"

out=$(aept_run "$root" mark protected nosuch 2>&1)
in_prot nosuch && fail "a package that is not installed was marked protected"
printf '%s\n' "$out" | grep -q "not installed" \
    || fail "marking an absent package protected went unremarked:
$out"
note "an absent name is not recorded, and is warned about"

# --all makes auto marks manual and leaves protected ones alone.
aept_run "$root" mark protected lone >/dev/null 2>&1
aept_run "$root" mark manual --all >/dev/null 2>&1 || fail "mark manual --all failed"
in_prot lone || fail "mark manual --all dropped a protection"
note "mark manual --all does not touch protected packages"

# ── a protected package cannot be removed by name ────────────────────

out=$(aept_run "$root" remove --non-interactive lone 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "removing a protected package exited 0:
$out"
installed lone || fail "the protected package was removed"
printf '%s\n' "$out" | grep -q "protected" \
    || fail "the refusal does not say why:
$out"
printf '%s\n' "$out" | grep -q "mark manual" \
    || fail "the refusal does not say how to lift it:
$out"
note "remove by name is refused, naming the way out"

aept_run "$root" mark manual lone >/dev/null 2>&1
aept_run "$root" remove --non-interactive lone >/dev/null 2>&1 \
    || fail "removing after mark manual failed"
installed lone && fail "lone survived removal after mark manual"
note "after mark manual it can go"

# ── ... nor as a dependant of the request ────────────────────────────
#
# Removing lib would take app with it (aept removes dependants).  With
# app protected, the solver has no solution and nothing changes.

aept_run "$root" install --non-interactive app >/dev/null 2>&1 \
    || fail "installing app failed"
in_auto lib || fail "lib was not marked auto as a dependency"
aept_run "$root" mark protected app >/dev/null 2>&1 || fail "mark protected app failed"

out=$(aept_run "$root" remove --non-interactive lib 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "removing the dependency of a protected package exited 0:
$out"
installed lib || fail "lib was removed from under a protected package"
installed app || fail "the protected dependant was removed"
note "the solver refuses to remove a protected package's dependency"

# ── ... nor to resolve a conflict ────────────────────────────────────

out=$(aept_run "$root" install --non-interactive rival 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "installing a package conflicting with a protected one's dependency exited 0:
$out"
installed rival && fail "rival was installed"
installed lib || fail "lib was removed to make room for rival"
note "a conflict cannot be resolved by removing a protected package"

# ── ... but it can still be upgraded ─────────────────────────────────

aept_run "$root" mark protected lib >/dev/null 2>&1 || fail "mark protected lib failed"
out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrading with a protected package exited $rc:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^lib - 2.0 ' \
    || fail "the protected package was not upgraded:
$(aept_run "$root" list --installed 2>&1)"
in_prot lib || fail "the upgrade dropped the protection"
note "a protected package is upgraded, and stays protected"

# ── installing it again by name keeps the mark ───────────────────────

aept_run "$root" install --non-interactive --reinstall lib >/dev/null 2>&1 \
    || fail "reinstalling lib failed"
in_prot lib || fail "reinstalling by name dropped the protection"
in_auto lib && fail "reinstalling by name marked it auto"
note "install by name leaves a protected package protected"

# The same for a package file named on the command line, which takes a
# different path to "this one is explicitly requested".
aept_run "$root" install --non-interactive --reinstall "$work/lib_2.0.aeltra" >/dev/null 2>&1 \
    || fail "reinstalling lib from a file failed"
in_prot lib || fail "reinstalling from a file dropped the protection"
note "install from a file leaves a protected package protected"

# ── a protected line for a package that is not installed is inert ────
#
# The mark outlives nothing -- a removal drops it -- but a hand edit or
# a root assembled from parts can leave one for a package that is not
# there.  It must not pull the package in, and must not stop anything.

# rival is in the index and not installed -- a name the solver knows,
# so "not installed" is the check that has to hold, not "unknown".
printf 'rival protected\n' >> "$marks_file"
out=$(aept_run "$root" install --non-interactive lone 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a protected line for an absent package broke a transaction:
$out"
installed rival && fail "a protected line pulled in a package that was not there"
installed lone || fail "lone was not installed"
note "a protected line for an absent package changes nothing"
aept_run "$root" remove --non-interactive lone >/dev/null 2>&1
sed -i '/^rival protected$/d' "$marks_file"

# ── autoremove never takes it ────────────────────────────────────────
#
# lib is protected; make app go away and lib would be an autoremove
# candidate if it were auto.  It is not in the auto set, so it is not.

aept_run "$root" mark manual app >/dev/null 2>&1
aept_run "$root" remove --non-interactive app >/dev/null 2>&1 || fail "removing app failed"
aept_run "$root" autoremove --non-interactive >/dev/null 2>&1 || fail "autoremove failed"
installed lib || fail "autoremove took a protected package"
note "autoremove leaves a protected package alone"

# ── a protected package does not disappear ───────────────────────────
#
# A bare Replaces that takes a package's last file makes it disappear.
# Protected means it stays, owning nothing, the same as when something
# depends on it.

mkdir -p "$work/t-taker/usr/bin"
printf 'taker\n' > "$work/t-taker/usr/bin/lone"
make_pkg_tree "$work/taker_1.0.aeltra" taker 1.0 "Replaces: lone" "$work/t-taker"
aept_run "$root" install --non-interactive "$work/lone_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing lone again failed"
aept_run "$root" mark protected lone >/dev/null 2>&1 || fail "mark protected lone failed"

out=$(aept_run "$root" install --non-interactive "$work/taker_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "taking over a protected package's last file exited $rc:
$out"
installed lone || fail "a protected package disappeared:
$out"
installed taker || fail "taker was not installed"
grep -q 'usr/bin/lone' "$root/var/lib/aept/info/lone.list" \
    && fail "lone still claims the path taker took"
note "a protected package whose files are all taken over stays, owning nothing"

# ── a hand-made duplicate line reads by its first, and is set once ───
#
# One file, one line per name, by construction of every write.  A hand
# edit can still duplicate a name; the reader takes the first line it
# meets, and the next write leaves exactly one.

aept_run "$root" mark manual taker lone >/dev/null 2>&1
aept_run "$root" remove --non-interactive taker >/dev/null 2>&1 || fail "removing taker failed"
aept_run "$root" install --non-interactive app >/dev/null 2>&1 || fail "installing app failed"
aept_run "$root" mark protected lib >/dev/null 2>&1 || fail "mark protected lib failed"
printf 'lib auto\n' >> "$marks_file"
[ "$(lines_for lib)" = 2 ] || fail "could not write a duplicate line"

out=$(aept_run "$root" remove --non-interactive lib 2>&1)
[ $? -ne 0 ] || fail "a name whose first line is protected was removed:
$out"

aept_run "$root" mark auto lib >/dev/null 2>&1 || fail "mark auto lib failed"
[ "$(lines_for lib)" = 1 ] || fail "the rewrite left $(lines_for lib) lines for lib:
$(cat "$marks_file")"
in_auto lib || fail "lib is not auto after the rewrite"
note "a duplicated name is read by its first line and written back once"
aept_run "$root" mark manual app >/dev/null 2>&1
aept_run "$root" remove --non-interactive app >/dev/null 2>&1 || fail "removing app failed"
aept_run "$root" mark manual lib >/dev/null 2>&1

# ── protecting a metapackage protects what it depends on ─────────────
#
# base depends on init, which two packages provide.  With base
# protected, the last provider cannot go, but one provider can still
# replace the other: the dependency stays satisfied.

make_pkg_tree "$work/base_1.0.aeltra" base 1.0 "Depends: init" "$work/t-base"
make_pkg_tree "$work/init-a_1.0.aeltra" init-a 1.0 "Provides: init
Conflicts: init
Replaces: init" "$work/t-init-a"
make_pkg_tree "$work/init-b_1.0.aeltra" init-b 1.0 "Provides: init
Conflicts: init
Replaces: init" "$work/t-init-b"
{
    packages_stanza base 1.0 "$work/base_1.0.aeltra" "Depends: init"
    packages_stanza init-a 1.0 "$work/init-a_1.0.aeltra" "Provides: init
Conflicts: init
Replaces: init"
    packages_stanza init-b 1.0 "$work/init-b_1.0.aeltra" "Provides: init
Conflicts: init
Replaces: init"
} >> "$root/var/lib/aept/lists/testrepo"
cp "$work"/base_1.0.aeltra "$work"/init-*.aeltra "$cache/"

aept_run "$root" install --non-interactive init-a base >/dev/null 2>&1 \
    || fail "installing init-a and base failed"
aept_run "$root" mark protected base >/dev/null 2>&1 || fail "mark protected base failed"

out=$(aept_run "$root" remove --non-interactive init-a 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "removing the only provider of a protected dependency exited 0:
$out"
installed init-a || fail "init-a was removed from under base"
note "the last provider of a protected package's dependency stays"

# Naming the virtual name removes its provider, so a protected provider
# is refused by that name too, and in words.
aept_run "$root" mark protected init-a >/dev/null 2>&1 || fail "mark protected init-a failed"
out=$(aept_run "$root" remove --non-interactive init 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "removing a protected provider by its virtual name exited 0:
$out"
printf '%s\n' "$out" | grep -q "init-a.*protected" \
    || fail "the refusal by virtual name does not name the protected provider:
$out"
installed init-a || fail "init-a was removed by its virtual name"
aept_run "$root" mark manual init-a >/dev/null 2>&1
note "remove by a virtual name is refused when the provider is protected"

out=$(aept_run "$root" install --non-interactive init-b 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "replacing one provider with another exited $rc:
$out"
installed init-b || fail "init-b was not installed"
installed init-a && fail "init-a survived being replaced"
installed base || fail "base was removed"
note "one provider can replace another; the dependency is what is kept"

exit 0
