#!/bin/sh
# test_provides.sh - virtual packages.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# A package may declare names other than its own (Policy 7.5), and those
# names satisfy dependencies, take part in conflicts, and can be asked
# for by name.  Everything here is the solver's reading of Provides;
# what "aept show" prints of it is test_show.sh's business.
#
# The implicit self-provide -- every package provides its own name at
# its own version -- is part of the same mechanism and is what makes an
# ordinary dependency resolve at all, so it is checked here too.

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
reset() { aept_run "$root" remove --non-interactive mailer exim postfix dovecot >/dev/null 2>&1; }

# Built with make_pkg_tree so the declarations live in each package's
# own control file, not only in the index stanza.  Once a package is
# installed its relationships are read back from the status database,
# which is written from that control -- an index-only Provides would
# vanish the moment the package became the installed one, which is
# exactly when the conflict below has to see it.
for p in mailer exim postfix dovecot; do
    mkdir -p "$work/$p/usr/share/$p"
    printf '%s\n' "$p" > "$work/$p/usr/share/$p/f"
done

make_pkg_tree "$work/mailer.aeltra"  mailer  1.0 "Depends: mta"  "$work/mailer"
make_pkg_tree "$work/exim.aeltra"    exim    1.0 "Provides: mta" "$work/exim"
make_pkg_tree "$work/postfix.aeltra" postfix 1.0 "Provides: mta" "$work/postfix"
make_pkg_tree "$work/dovecot.aeltra" dovecot 1.0 "Provides: mta
Conflicts: mta" "$work/dovecot"

add_repo "$root" testrepo "$work"
cp "$work"/*.aeltra "$cache/"

# ── a dependency is satisfied by a provider ──────────────────────────
#
# mailer needs "mta", which no package is called.  exim provides it.

{
    packages_stanza mailer 1.0 "$work/mailer.aeltra" "Depends: mta"
    packages_stanza exim 1.0 "$work/exim.aeltra" "Provides: mta"
} > "$list"

out=$(aept_run "$root" install --non-interactive mailer 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "a dependency on a virtual name was not satisfied, exit $rc:
$out"
installed mailer || fail "mailer was not installed:
$out"
installed exim || fail "the provider of the virtual name was not pulled in:
$out"
note "a dependency on a virtual name is satisfied by its provider"

# ── a dependency nothing provides is refused ─────────────────────────

reset
{
    packages_stanza mailer 1.0 "$work/mailer.aeltra" "Depends: mta"
    packages_stanza exim 1.0 "$work/exim.aeltra"
} > "$list"
# exim's own control still declares the Provides; the index is what the
# solver reads when choosing, and here it offers nothing that provides
# "mta".

out=$(aept_run "$root" install --non-interactive mailer 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a dependency nothing provides was accepted:
$out"
installed mailer && fail "mailer was installed with an unsatisfied dependency:
$out"
note "and refused when nothing provides it"

# ── the virtual name can be asked for by name ────────────────────────

reset
{
    packages_stanza exim 1.0 "$work/exim.aeltra" "Provides: mta"
} > "$list"

out=$(aept_run "$root" install --non-interactive mta 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing a virtual name exited $rc:
$out"
installed exim || fail "installing the virtual name did not install its provider:
$out"
note "installing a virtual name installs its provider"

# ── one of several providers satisfies it ────────────────────────────
#
# Which one is the solver's choice and not pinned here; that exactly one
# arrives is.

reset
{
    packages_stanza mailer 1.0 "$work/mailer.aeltra" "Depends: mta"
    packages_stanza exim 1.0 "$work/exim.aeltra" "Provides: mta"
    packages_stanza postfix 1.0 "$work/postfix.aeltra" "Provides: mta"
} > "$list"

out=$(aept_run "$root" install --non-interactive mailer 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "two providers of one virtual name exited $rc:
$out"

n=0
installed exim && n=$((n + 1))
installed postfix && n=$((n + 1))
[ "$n" -eq 1 ] || fail "expected exactly one provider to be installed, got $n:
$out"
note "one of several providers satisfies the dependency"

# ── a conflict against a virtual name reaches its provider ───────────
#
# Policy 7.6.2's mail-transport-agent shape: the conflict is declared
# against the virtual name, and the package it must remove is the real
# one providing it.

reset
{
    packages_stanza exim 1.0 "$work/exim.aeltra" "Provides: mta"
    packages_stanza dovecot 1.0 "$work/dovecot.aeltra" "Provides: mta
Conflicts: mta"
} > "$list"

aept_run "$root" install --non-interactive exim >/dev/null 2>&1 \
    || fail "installing exim failed"
installed exim || fail "exim is not installed to begin with"

out=$(aept_run "$root" install --non-interactive dovecot 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing over a virtual conflict exited $rc:
$out"
installed dovecot || fail "dovecot was not installed:
$out"
installed exim && fail "the provider of the conflicted virtual name survived:
$out"
note "a conflict on a virtual name removes the package providing it"

exit 0
