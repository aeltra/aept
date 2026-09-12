#!/bin/sh
# test_show.sh - what "aept show" prints.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# The output is a contract: it is what a person reads to decide whether
# to install something, and what a script greps.  Every field is
# conditional on the package carrying it, and the description is
# reformatted rather than copied -- continuation lines are re-indented
# one space at a time.  None of that had ever run: the API-level test
# calls aept_show(), and the smoke test only asks for a package that is
# not there, so the printing below the lookup was reached by nothing.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
info=$root/var/lib/aept/info
mkdir -p "$info"

# The fields below are asserted in the form the package declares them:
# the parentheses of "libx (>= 1.0)", a Provides without the implicit
# self-provide, and a Replaces that is the Replaces.  All three used to
# come from libsolv's pool, which renders, appends and conflates for the
# solver's benefit -- correct there, wrong under a Debian field name.
#
# An installed package carrying every optional field.  Written by hand:
# a .control and a .list are just files, and going through an install
# would settle for whatever the fixture builder happens to emit.
cat > "$info/showcase.control" <<'EOF'
Package: showcase
Version: 2.1-3
Architecture: all
Maintainer: t <t@example.invalid>
Section: utils
Source: showsrc
Installed-Size: 42
Depends: libx (>= 1.0)
Pre-Depends: libpre
Recommends: librec
Suggests: libsug
Provides: virtual-thing
Conflicts: libcon
Replaces: librep
Homepage: https://example.invalid/showcase
Description: a one-line summary
 the first continuation line
 the second continuation line
Status: install ok installed
EOF
printf './usr/bin/showcase\t100755\n' > "$info/showcase.list"

out=$(aept_run "$root" show showcase 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show of an installed package exited $rc:
$out"

for field in \
    'Package: showcase' \
    'Version: 2.1-3' \
    'Architecture: all' \
    'Section: utils' \
    'Source: showsrc' \
    'Maintainer: t <t@example.invalid>' \
    'Installed-Size: 42 kB' \
    'Depends: libx (>= 1.0)' \
    'Pre-Depends: libpre' \
    'Recommends: librec' \
    'Suggests: libsug' \
    'Provides: virtual-thing' \
    'Conflicts: libcon' \
    'Replaces: librep' \
    'Homepage: https://example.invalid/showcase' \
    'Description: a one-line summary' \
    'Status: install ok installed'
do
    printf '%s\n' "$out" | grep -qF "$field" \
        || fail "show did not print '$field':
$out"
done
note "every field the package carries is printed"

# libsolv adds "showcase = 2.1-3" to every package's Provides for its own
# purposes.  That is not something the packager wrote, so it must not
# appear under a field name that says they did.
printf '%s\n' "$out" | grep -q '^Provides:.*showcase' \
    && fail "the solver's implicit self-provide leaked into Provides:
$out"
note "Provides is what the package declared, without the self-provide"

# The summary is the first line of Description; the rest follow it,
# each indented by one space.  A package whose description went missing
# between the two is the failure this catches.
printf '%s\n' "$out" | grep -qx ' the first continuation line' \
    || fail "the first continuation line is missing or misindented:
$out"
printf '%s\n' "$out" | grep -qx ' the second continuation line' \
    || fail "the second continuation line is missing or misindented:
$out"
note "the description continuation lines follow the summary, one space in"

# A description of one line is one line.  libsolv reports the summary
# again as the description when a package has no continuation -- which
# is every stanza in an archive whose descriptions are one line -- and
# printing both said the same sentence twice.
cat > "$info/terse.control" <<'EOF'
Package: terse
Version: 1.0
Architecture: all
Description: all there is to say
Status: install ok installed
EOF
printf './usr/bin/terse\t100755\n' > "$info/terse.list"
out=$(aept_run "$root" show terse 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show of a one-line description exited $rc:
$out"
[ "$(printf '%s\n' "$out" | grep -c 'all there is to say')" = 1 ] \
    || fail "a one-line description was printed more than once:
$out"
note "a one-line description is printed once"

# ── a package that is available but not installed ────────────────────
#
# Filename comes from the index and Status from the status area, so
# this is the case that has neither the one nor the other.

cache=$root/var/cache/aept
list=$root/var/lib/aept/lists/testrepo
mkdir -p "$cache"
mkdir -p "$work/tree/usr/share/shelf"
printf 's\n' > "$work/tree/usr/share/shelf/f"
make_pkg_tree "$work/shelf_1.0.aeltra" shelf 1.0 "" "$work/tree"
add_repo "$root" testrepo "$work"
packages_stanza shelf 1.0 "$work/shelf_1.0.aeltra" > "$list"

out=$(aept_run "$root" show shelf 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show of an available package exited $rc:
$out"
printf '%s\n' "$out" | grep -qF 'Package: shelf' \
    || fail "show of an available package printed no name:
$out"
printf '%s\n' "$out" | grep -qF 'Filename: ' \
    || fail "show of an available package printed no Filename:
$out"
printf '%s\n' "$out" | grep -qE '^Download-Size: [0-9]+ kB$' \
    || fail "show of an available package printed no Download-Size:
$out"
printf '%s\n' "$out" | grep -qF 'Status: install ok installed' \
    && fail "an uninstalled package was reported as installed:
$out"
note "an available package shows its Filename and no Status"

# ── the candidate, and every version ─────────────────────────────────
#
# "show" answers with the candidate, which is commonly not the version
# on disk.  The Status line has to describe the version in the stanza it
# appears in: saying "installed" under a 2.0 stanza while 1.0 is what is
# installed is how somebody concludes they are already upgraded.

mkdir -p "$work/v1/usr/share/two" "$work/v2/usr/share/two"
printf '1\n' > "$work/v1/usr/share/two/f"
printf '2\n' > "$work/v2/usr/share/two/f"
make_pkg_tree "$work/two_1.0.aeltra" two 1.0 "Homepage: https://old.invalid" "$work/v1"
make_pkg_tree "$work/two_2.0.aeltra" two 2.0 "Homepage: https://new.invalid" "$work/v2"
{
    packages_stanza shelf 1.0 "$work/shelf_1.0.aeltra"
    packages_stanza two 1.0 "$work/two_1.0.aeltra" "Homepage: https://old.invalid"
    packages_stanza two 2.0 "$work/two_2.0.aeltra" "Homepage: https://new.invalid"
} > "$list"
cp "$work/two_1.0.aeltra" "$work/two_2.0.aeltra" "$cache/"
aept_run "$root" install --non-interactive "$work/two_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing two 1.0 failed"

out=$(aept_run "$root" show two 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show of the candidate exited $rc:
$out"
printf '%s\n' "$out" | grep -qF 'Version: 2.0' \
    || fail "show did not answer with the candidate:
$out"
printf '%s\n' "$out" | grep -qF 'Status: install ok installed' \
    && fail "the candidate 2.0 was reported installed while 1.0 is:
$out"
note "show answers with the candidate, and does not call it installed"

out=$(aept_run "$root" show -a two 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show -a exited $rc:
$out"
[ "$(printf '%s\n' "$out" | grep -c '^Package: two$')" = 2 ] \
    || fail "show -a did not print one stanza per version:
$out"
[ "$(printf '%s\n' "$out" | grep -n '^Version:' | head -1 | cut -d: -f2-)" = "1:Version: 2.0" ] \
    || [ "$(printf '%s\n' "$out" | grep '^Version:' | head -1)" = "Version: 2.0" ] \
    || fail "show -a did not lead with the newest version:
$out"
[ "$(printf '%s\n' "$out" | grep -c '^Status: install ok installed$')" = 1 ] \
    || fail "show -a marked other than exactly one version installed:
$out"
# ... and it is the 1.0 stanza that carries it.
printf '%s\n' "$out" | awk '/^Version: 1.0$/,/^$/' | grep -qF 'Status: install ok installed' \
    || fail "the installed marker is not on the 1.0 stanza:
$out"
note "show -a prints every version, newest first, marking the installed one"

# The long form, because a missing entry in the option table is exactly
# the kind of thing that goes unnoticed until somebody types it out.
long=$(aept_run "$root" show --all two 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show --all exited $rc:
$long"
[ "$long" = "$out" ] || fail "--all and -a disagree:
--- --all ---
$long
--- -a ---
$out"
note "--all is the long form of -a"

out=$(aept_run "$root" show -a absent 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "show -a of an unknown package succeeded:
$out"
note "show -a of an unknown package is refused too"

# ── the candidate is the one an install would take ───────────────────
#
# "show" is the command somebody runs before installing, so the version
# it names has to be the version they would get.  It answered with the
# newest in the archive regardless of what the machine would accept.

aept_run "$root" pin two=1.0 >/dev/null 2>&1 || fail "pinning two failed"
out=$(aept_run "$root" show two 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show under a pin exited $rc:
$out"
printf '%s\n' "$out" | grep -qF 'Version: 1.0' \
    || fail "show ignored the pin and named another version:
$out"
note "a pinned package shows the version it is pinned to"

aept_run "$root" unpin two >/dev/null 2>&1 || fail "unpinning two failed"
out=$(aept_run "$root" show two 2>&1)
printf '%s\n' "$out" | grep -qF 'Version: 2.0' \
    || fail "the candidate did not return to the newest once unpinned:
$out"
note "and the newest again once the pin is gone"

# A build for an architecture the machine does not take is not a
# version it could have, so it is neither the candidate nor listed.
mkdir -p "$work/other/usr/share/two"
printf 'o\n' > "$work/other/usr/share/two/f"
make_pkg_tree "$work/two_9.0.aeltra" two 9.0 "" "$work/other"
{
    packages_stanza shelf 1.0 "$work/shelf_1.0.aeltra"
    packages_stanza two 1.0 "$work/two_1.0.aeltra" "Homepage: https://old.invalid"
    packages_stanza two 2.0 "$work/two_2.0.aeltra" "Homepage: https://new.invalid"
    printf 'Package: two\nVersion: 9.0\nArchitecture: nosucharch\n'
    printf 'Filename: two_9.0.aeltra\nSize: 100\n'
    printf 'SHA256: %064d\nDescription: for another machine\n\n' 1
} > "$list"

out=$(aept_run "$root" show two 2>&1)
printf '%s\n' "$out" | grep -qF 'Version: 9.0' \
    && fail "a build for an unconfigured architecture was named the candidate:
$out"
printf '%s\n' "$out" | grep -qF 'Version: 2.0' \
    || fail "the candidate is not the newest installable version:
$out"
note "a build for an unconfigured architecture is not the candidate"

out=$(aept_run "$root" show -a two 2>&1)
printf '%s\n' "$out" | grep -qF 'Version: 9.0' \
    && fail "show -a listed a version the machine cannot install:
$out"
note "nor is it listed among the versions"

# ── the ways it declines ─────────────────────────────────────────────

out=$(aept_run "$root" show absent 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "show of an unknown package succeeded:
$out"
case $out in
    *"not found"*) ;;
    *) fail "show of an unknown package gave no useful error:
$out" ;;
esac
note "an unknown package is reported as not found"

out=$(aept_run "$root" show 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "show with no package name succeeded:
$out"
note "show with no package name is refused"

out=$(aept_run "$root" show --help 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "show --help exited $rc:
$out"
case $out in
    Usage:*) ;;
    *) fail "show --help printed no usage:
$out" ;;
esac
note "show --help prints usage and succeeds"

exit 0
