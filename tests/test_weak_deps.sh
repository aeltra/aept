#!/bin/sh
# test_weak_deps.sh - Recommends and Suggests are not requirements.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Debian has three strengths of "you probably want this too" and aept
# treats none of them as a dependency:
#
#   Recommends  strong, but not required (Policy 7.2)
#   Suggests    weaker still
#
# libsolv satisfies Recommends by default, which is apt's behaviour and
# the wrong one here: aept's targets are often small, and a package
# pulling in whatever its recommends name -- transitively -- is not what
# an embedded root wants.  So the solver is told to ignore them, and
# "option install_recommends 1" asks for apt's behaviour back.
#
# Nothing about this was chosen before it was tested: the default came
# from libsolv, recommends *were* installed, and no test said so.

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
mkdir -p "$cache"

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }

make_pkg "$work/app.aeltra"   app   1.0
make_pkg "$work/nice.aeltra"  nice  1.0
make_pkg "$work/maybe.aeltra" maybe 1.0
make_pkg "$work/need.aeltra"  need  1.0

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

{
    packages_stanza app 1.0 "$work/app.aeltra" "Depends: need
Recommends: nice
Suggests: maybe"
    packages_stanza need 1.0 "$work/need.aeltra"
    packages_stanza nice 1.0 "$work/nice.aeltra"
    packages_stanza maybe 1.0 "$work/maybe.aeltra"
} > "$list"

# ── neither is installed by default ──────────────────────────────────

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing app exited $rc:
$out"

installed app  || fail "app was not installed:
$out"
installed need || fail "a Depends was not installed -- the test fixture is wrong:
$out"
installed nice  && fail "a Recommends was installed without being asked for:
$out"
installed maybe && fail "a Suggests was installed without being asked for:
$out"
note "Recommends and Suggests are not installed by default"

# ── a recommended package is installable on its own ───────────────────
#
# Ignoring the field must not make the package unreachable.

out=$(aept_run "$root" install --non-interactive nice 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing the recommended package by name exited $rc:
$out"
installed nice || fail "the recommended package could not be installed by name:
$out"
note "and the package itself is still installable by name"

# ── option install_recommends 1 asks for apt's behaviour ─────────────

aept_run "$root" remove --non-interactive app nice need >/dev/null 2>&1
printf 'option install_recommends 1\n' >> "$root/etc/aept/aept.conf"

out=$(aept_run "$root" install --non-interactive app 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing app with install_recommends exited $rc:
$out"

installed nice || fail "install_recommends 1 did not pull the Recommends in:
$out"
installed maybe && fail "install_recommends must not reach Suggests:
$out"
note "option install_recommends 1 installs Recommends, still not Suggests"

exit 0
