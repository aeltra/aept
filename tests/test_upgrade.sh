#!/bin/sh
# test_upgrade.sh - an upgrade replaces the old version completely: the
# files the new version dropped must go, the ones it added must land,
# and the status database must say exactly one version at every point.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
info=$root/var/lib/aept/info

version_of() {
    aept_run "$root" list --installed 2>/dev/null | sed -n "s/^$1 - \([^ ]*\).*/\1/p"
}

# v1: a tool, a file that will vanish in v2, and a directory of its own.
mkdir -p "$work/v1/usr/bin" "$work/v1/usr/share/tool"
printf 'tool 1.0\n' > "$work/v1/usr/bin/tool"
printf 'v1 only\n' > "$work/v1/usr/share/tool/old-only"
make_pkg_tree "$work/tool_1.0.aeltra" tool 1.0 "" "$work/v1"

# v2: the tool changed, old-only is gone, new-only appears.
mkdir -p "$work/v2/usr/bin" "$work/v2/usr/share/tool"
printf 'tool 2.0\n' > "$work/v2/usr/bin/tool"
printf 'v2 only\n' > "$work/v2/usr/share/tool/new-only"
make_pkg_tree "$work/tool_2.0.aeltra" tool 2.0 "" "$work/v2"

# ── upgrade: old files out, new files in, one version registered ─────

aept_run "$root" install --non-interactive "$work/tool_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing tool 1.0 failed"

out=$(aept_run "$root" install --non-interactive "$work/tool_2.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrading to 2.0 exited $rc:
$out"
[ "$(version_of tool)" = "2.0" ] || fail "registered version is '$(version_of tool)', not 2.0"
grep -q 'tool 2.0' "$root/usr/bin/tool" || fail "the tool was not replaced"
[ -f "$root/usr/share/tool/new-only" ] || fail "the new file did not land"
[ ! -f "$root/usr/share/tool/old-only" ] \
    || fail "the file the new version dropped was left behind"
grep -q 'old-only' "$info/tool.list" \
    && fail "the .list still names the dropped file"
grep -q 'new-only' "$info/tool.list" \
    || fail "the .list does not name the new file"
note "1.0 -> 2.0: dropped file deleted, new file landed, list rewritten"

# ── an explicit local file downgrades, dpkg-style ────────────────────
#
# Pinned as observed: handing aept a specific .aeltra is an explicit
# SOLVER_INSTALL job on that exact solvable, which libsolv honours
# regardless of SOLVER_FLAG_ALLOW_DOWNGRADE -- the flag gates the
# solver's own choices (by-name and upgrade resolution), not a package
# the user pointed at.  Same reading of intent as `dpkg -i older.deb`;
# apt would ask for --allow-downgrades here.  If that divergence is
# ever unwanted, this is the assertion to flip.

out=$(aept_run "$root" install --non-interactive "$work/tool_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the explicit downgrade exited $rc:
$out"
[ "$(version_of tool)" = "1.0" ] || fail "not downgraded: $(version_of tool)"
grep -q 'tool 1.0' "$root/usr/bin/tool" || fail "the payload was not downgraded"
[ -f "$root/usr/share/tool/old-only" ] || fail "1.0's file did not come back"
[ ! -f "$root/usr/share/tool/new-only" ] || fail "2.0's file survived the downgrade"
note "an explicit local file downgrades, and the file sets swap accordingly"

# ── --reinstall restores a damaged install of the same version ───────

rm -f "$root/usr/bin/tool"

out=$(aept_run "$root" install --non-interactive "$work/tool_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "the same-version install exited $rc:
$out"
[ ! -f "$root/usr/bin/tool" ] \
    || fail "a plain install of the same version reinstalled; it should be a no-op"
printf '%s\n' "$out" | grep -q "already installed" \
    || fail "the no-op was not explained:
$out"

out=$(aept_run "$root" install --non-interactive --reinstall "$work/tool_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "--reinstall exited $rc:
$out"
grep -q 'tool 1.0' "$root/usr/bin/tool" \
    || fail "--reinstall did not restore the deleted payload"
[ "$(version_of tool)" = "1.0" ] || fail "reinstall changed the version"
note "--reinstall restores the payload; without it, same-version is a no-op"

# ── upgrade runs the maintainer scripts in upgrade mode ──────────────
#
# The old prerm and the new preinst both run with "upgrade" as their
# first argument -- a script seeing "remove" mid-upgrade would tear
# down state the new version is about to need.  This needs the scripts
# to actually run, so the root gets a shell.

provision_shell "$root" || skip "cannot provision a shell into the offline root"

mkdir -p "$work/s1/usr/bin" "$work/s2/usr/bin"
printf 's 1.0\n' > "$work/s1/usr/bin/s"
printf 's 2.0\n' > "$work/s2/usr/bin/s"
make_pkg_tree "$work/s_1.0.aeltra" s 1.0 "" "$work/s1"
make_pkg_tree "$work/s_2.0.aeltra" s 2.0 "" "$work/s2"

# Rebuild both with argument-recording scripts.
for v in 1.0 2.0; do
    d=$(mktemp -d)
    mkdir -p "$d/c"
    printf 'Package: s\nVersion: %s\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' "$v" > "$d/c/control"
    for script in prerm preinst; do
        printf '#!/bin/sh\necho "%s %s: $*" >> /args.log\nexit 0\n' "$v" "$script" > "$d/c/$script"
        chmod 755 "$d/c/$script"
    done
    tar czf "$d/control.tar.gz" -C "$d/c" control prerm preinst
    tar czf "$d/data.tar.gz" -C "$work/s${v%%.*}" .
    printf '2.0\n' > "$d/debian-binary"
    ( cd "$d" && ar rc "$work/s_$v.aeltra" debian-binary control.tar.gz data.tar.gz )
    rm -rf "$d"
done

aept_run "$root" install --non-interactive "$work/s_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing s 1.0 failed"
: > "$root/args.log"
aept_run "$root" install --non-interactive "$work/s_2.0.aeltra" >/dev/null 2>&1 \
    || fail "upgrading s failed"

grep -q '^1.0 prerm: upgrade 2.0$' "$root/args.log" \
    || fail "the old prerm did not run with 'upgrade <new-version>':
$(cat "$root/args.log")"
grep -q '^2.0 preinst: upgrade 1.0$' "$root/args.log" \
    || fail "the new preinst did not run with 'upgrade <old-version>':
$(cat "$root/args.log")"
note "old prerm and new preinst both saw 'upgrade' and the right version"

# ── the auto flag survives an upgrade-as-dependency ──────────────────
#
# The auto mark is set only for packages the *solver* chose -- a local
# file on the command line is explicit by definition -- so dep has to
# come from a repository.  The package list is seeded directly and the
# .aeltra files pre-placed in the cache, where the checksum admits
# them without a network.  What must hold: dep is marked auto when
# pulled in, and upgrading it as a dependency does not launder the
# mark away -- autoremove reasons from it, and a dependency that
# upgrades itself into "manually installed" never becomes removable.

require_tools sha256sum

auto_file=$root/var/lib/aept/auto-installed
cache=$root/var/cache/aept
list=$root/var/lib/aept/lists/testrepo
mkdir -p "$cache"

mkdir -p "$work/dep1/usr/share/dep" "$work/dep2/usr/share/dep" "$work/app1/usr/share/app"
printf 'dep 1\n' > "$work/dep1/usr/share/dep/v"
printf 'dep 2\n' > "$work/dep2/usr/share/dep/v"
printf 'app\n' > "$work/app1/usr/share/app/v"
make_pkg_tree "$work/dep_1.0.aeltra" dep 1.0 "" "$work/dep1"
make_pkg_tree "$work/dep_2.0.aeltra" dep 2.0 "" "$work/dep2"
make_pkg_tree "$work/app_1.0.aeltra" app 1.0 "Depends: dep" "$work/app1"
make_pkg_tree "$work/app_2.0.aeltra" app 2.0 "Depends: dep (>= 2.0)" "$work/app1"

add_repo "$root" testrepo "$work"
packages_stanza dep 1.0 "$work/dep_1.0.aeltra" > "$list"
cp "$work/dep_1.0.aeltra" "$cache/"

aept_run "$root" install --non-interactive "$work/app_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing app with dep from the repo failed"
[ "$(version_of dep)" = "1.0" ] || fail "dep was not pulled in: '$(version_of dep)'"
grep -q '^dep$' "$auto_file" \
    || fail "dep, pulled in as a dependency, was not marked auto:
$(cat "$auto_file" 2>/dev/null)"
grep -q '^app$' "$auto_file" \
    && fail "app, asked for by name, was marked auto"

packages_stanza dep 2.0 "$work/dep_2.0.aeltra" > "$list"
cp "$work/dep_2.0.aeltra" "$cache/"

aept_run "$root" install --non-interactive "$work/app_2.0.aeltra" >/dev/null 2>&1 \
    || fail "upgrading app (and dep with it) failed"
[ "$(version_of dep)" = "2.0" ] || fail "dep was not upgraded: '$(version_of dep)'"
grep -q '^dep$' "$auto_file" \
    || fail "dep lost its auto mark across the upgrade:
$(cat "$auto_file" 2>/dev/null)"
note "dep stays auto-installed through an upgrade as a dependency"

exit 0
