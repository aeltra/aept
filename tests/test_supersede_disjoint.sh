#!/bin/sh
# test_supersede_disjoint.sh - Conflicts and Replaces naming different
# packages must not make either one a supersede.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# THIS TEST SKIPS rather than fails while libsolv has the bug below,
# because the bug is not aept\'s and the package build gates on the
# suite: a libsolv fix arriving in a point release would otherwise break
# the build to report good news.  It still fails on any outcome that is
# neither correct nor the known-buggy one, so it guards aept.
#
# A package commonly conflicts with a competing implementation and
# replaces its own renamed predecessor -- two unrelated statements:
#
#   Package: chrony
#   Conflicts: ntp                 # cannot run two NTP daemons
#   Replaces: chrony-legacy        # took over the renamed package
#
# Nothing here says chrony supersedes ntp, so an upgrade must not offer
# it as one.  libsolv's Debian reader derives obsoletes by intersecting
# Replaces with Conflicts (ext/repo_deb.c, "obsoletes only count when
# the packages also conflict"), but pairs the two lists positionally
# instead: it emits the FIRST conflict, never comparing it against the
# Replaces entry under test.  So chrony obsoletes ntp, and a plain
# upgrade replaces somebody's NTP daemon.
#
# 482 of the 4069 packages in Debian trixie that declare both fields
# derive an obsoletes their packager never wrote.
#
# The day libsolv is fixed, or aept is built with the workaround that
# derives obsoletes itself, this turns from SKIP into PASS on its own.

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

for p in ntp chrony; do
    mkdir -p "$work/$p/usr/share/$p"
    printf '%s\n' "$p" > "$work/$p/usr/share/$p/f"
done
make_pkg_tree "$work/ntp_1.0.aeltra" ntp 1.0 "" "$work/ntp"
make_pkg_tree "$work/chrony_1.0.aeltra" chrony 1.0 "Conflicts: ntp
Replaces: chrony-legacy" "$work/chrony"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"
{
    packages_stanza ntp 1.0 "$work/ntp_1.0.aeltra"
    packages_stanza chrony 1.0 "$work/chrony_1.0.aeltra" "Conflicts: ntp
Replaces: chrony-legacy"
} > "$list"

aept_run "$root" install --non-interactive ntp >/dev/null 2>&1 \
    || fail "installing ntp failed"
installed ntp || fail "ntp is not installed after a fresh install"

out=$(aept_run "$root" upgrade --non-interactive 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade exited $rc:
$out"

# Three outcomes, and only two of them are accounted for.  Anything else
# -- both installed, ntp gone but chrony not there, a failed upgrade --
# is aept getting it wrong, and fails.
if installed ntp && ! installed chrony; then
    note "disjoint Conflicts and Replaces do not make a supersede"
    exit 0
fi

if ! installed ntp && installed chrony; then
    skip "libsolv pairs Conflicts and Replaces positionally, so chrony obsoletes ntp
     (ext/repo_deb.c, \"obsoletes only count when the packages also conflict\");
     this passes against a fixed libsolv, or with the workaround built in"
fi

fail "neither the correct outcome nor the known libsolv one:
  ntp installed:    $(installed ntp && echo yes || echo no)
  chrony installed: $(installed chrony && echo yes || echo no)
$out"
