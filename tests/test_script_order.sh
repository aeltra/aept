#!/bin/sh
# test_script_order.sh - when each maintainer script runs, and when not.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# The install, upgrade and remove paths each call a fixed sequence of
# maintainer scripts, and the sequence is a contract the scripts are
# written against: a prerm that stops a daemon relies on a postinst
# starting it again, a postrm that tidies up relies on the files it
# tidies still being there.  Every package here ships all four scripts,
# and each one appends its name and arguments to /script.log inside
# the root, so a section can assert the whole sequence and not just
# that some script ran.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
provision_shell "$root" || skip "no shell to run maintainer scripts"
mkdir -p "$root/var/cache/aept"

log=$root/script.log

installed() { aept_run "$root" list --installed 2>/dev/null | grep -q "^$1 "; }
ran() { grep -q "^$1\$" "$log" 2>/dev/null; }

# scripts <dir> <tag> [failing-script] -- write the four scripts into
# <dir>, each logging "<tag> <script> <args>"; the named one exits 1
# after logging.
scripts() {
    _dir=$1 _tag=$2 _bad=${3:-}
    mkdir -p "$_dir"
    for _s in preinst postinst prerm postrm; do
        printf '#!/bin/sh\necho "%s %s $*" >> /script.log\n' "$_tag" "$_s" > "$_dir/$_s"
        [ "$_s" = "$_bad" ] && printf 'exit 1\n' >> "$_dir/$_s"
    done
}

# ── a clash is found before any script has run ───────────────────────
#
# A file clash is an ordinary refusal, and the check needs only the
# archive listing and the owner index.  Running it after preinst means
# a refused install has already had side effects nothing will undo;
# running it after the old prerm on an upgrade means the old version
# has been told it is going away when it is not.

mkdir -p "$work/owner/usr/bin" "$work/clash/usr/bin"
printf 'owner\n' > "$work/owner/usr/bin/shared"
printf 'clash\n' > "$work/clash/usr/bin/shared"

scripts "$work/s-owner" owner
scripts "$work/s-clash" clash
make_pkg_scripts "$work/owner_1.0.aeltra" owner 1.0 "" "$work/owner" "$work/s-owner"
make_pkg_scripts "$work/clash_1.0.aeltra" clash 1.0 "" "$work/clash" "$work/s-clash"

aept_run "$root" install --non-interactive "$work/owner_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing owner failed"
ran "owner preinst install" || fail "owner's preinst did not run:
$(cat "$log")"
ran "owner postinst configure" || fail "owner's postinst did not run:
$(cat "$log")"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/clash_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a clashing install was allowed:
$out"
installed clash && fail "the clashing package is installed"
[ -s "$log" ] && fail "a refused install ran a maintainer script:
$(cat "$log")"
note "a fresh install that clashes runs no script"

# The same on an upgrade: owner 2.0 clashes with a third package.
mkdir -p "$work/third/usr/bin" "$work/owner2/usr/bin"
printf 'third\n' > "$work/third/usr/bin/third"
printf 'owner\n' > "$work/owner2/usr/bin/shared"
printf 'owner\n' > "$work/owner2/usr/bin/third"

scripts "$work/s-third" third
scripts "$work/s-owner2" owner-2.0
make_pkg_scripts "$work/third_1.0.aeltra" third 1.0 "" "$work/third" "$work/s-third"
make_pkg_scripts "$work/owner_2.0.aeltra" owner 2.0 "" "$work/owner2" "$work/s-owner2"

aept_run "$root" install --non-interactive "$work/third_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing third failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/owner_2.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a clashing upgrade was allowed:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^owner - 1.0 ' \
    || fail "owner 1.0 is no longer the installed version"
[ -s "$log" ] && fail "a refused upgrade ran a maintainer script:
$(cat "$log")"
note "an upgrade that clashes runs no script, old prerm included"

# ── the old postrm runs while the old files are still there ──────────
#
# On an upgrade the old version's postrm runs after the new files are
# unpacked and *before* the files the new version no longer ships are
# deleted -- it may need them, a helper it installed being the usual
# case.  So the whole upgrade sequence is asserted here, and the old
# postrm reports whether the dropped file was still on disk.

aept_run "$root" remove --non-interactive third >/dev/null 2>&1 \
    || fail "removing third failed"

mkdir -p "$work/up1/usr/bin" "$work/up2/usr/bin"
printf 'helper\n' > "$work/up1/usr/bin/helper"
printf 'up\n' > "$work/up1/usr/bin/up"
printf 'up\n' > "$work/up2/usr/bin/up"

scripts "$work/s-up1" up-1.0
scripts "$work/s-up2" up-2.0
printf 'test -f /usr/bin/helper && echo "helper present" >> /script.log\n' \
    >> "$work/s-up1/postrm"
make_pkg_scripts "$work/up_1.0.aeltra" up 1.0 "" "$work/up1" "$work/s-up1"
make_pkg_scripts "$work/up_2.0.aeltra" up 2.0 "" "$work/up2" "$work/s-up2"

aept_run "$root" install --non-interactive "$work/up_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing up 1.0 failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/up_2.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrading up exited $rc:
$out"

expected='up-1.0 prerm upgrade 2.0
up-2.0 preinst upgrade 1.0 2.0
up-1.0 postrm upgrade 2.0
helper present
up-2.0 postinst configure 1.0'
[ "$(cat "$log")" = "$expected" ] || fail "upgrade ran the scripts in the wrong order, or
the old postrm ran after its files were gone:
$(cat "$log")"
[ -e "$root/usr/bin/helper" ] && fail "the dropped file survived the upgrade"
note "upgrade: old prerm, new preinst, old postrm (files intact), new postinst"

# ── a takeover is committed once the files are on disk ───────────────
#
# The old owner's .list is rewritten as soon as the new package's files
# are unpacked, not once its postinst has succeeded: the files belong
# to the new package from the moment they are on disk, whatever its
# configure step does afterwards.  Left unrewritten, the old owner's
# removal would delete a file that is not its any more.

mkdir -p "$work/keeper/usr/bin" "$work/grab/usr/bin"
printf 'keeper\n' > "$work/keeper/usr/bin/k1"
printf 'keeper\n' > "$work/keeper/usr/bin/k2"
printf 'grab\n' > "$work/grab/usr/bin/k1"

scripts "$work/s-keeper" keeper
scripts "$work/s-grab" grab postinst
make_pkg_scripts "$work/keeper_1.0.aeltra" keeper 1.0 "" "$work/keeper" "$work/s-keeper"
make_pkg_scripts "$work/grab_1.0.aeltra" grab 1.0 "Replaces: keeper" "$work/grab" "$work/s-grab"

aept_run "$root" install --non-interactive "$work/keeper_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing keeper failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/grab_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an install whose postinst failed exited 0:
$out"
ran "grab postinst configure" || fail "grab's postinst did not run:
$(cat "$log")"
grep -q 'usr/bin/k1' "$root/var/lib/aept/info/keeper.list" \
    && fail "keeper still claims the path grab took, because grab's postinst failed"
grep -q 'usr/bin/k2' "$root/var/lib/aept/info/keeper.list" \
    || fail "keeper lost the path it kept"

aept_run "$root" remove --non-interactive keeper >/dev/null 2>&1 \
    || fail "removing keeper failed"
grep -q '^grab$' "$root/usr/bin/k1" \
    || fail "removing keeper deleted the file grab had taken over"
note "a failed postinst does not undo the takeover of the files"

# ── a package disappears before the overwriter is configured ─────────
#
# The disappearing package's postrm runs in the unpack phase, before
# the overwriter's postinst: by then its files are already the
# overwriter's, and the overwriter's postinst may rely on the old
# package having cleaned up after itself.

aept_run "$root" remove --non-interactive grab >/dev/null 2>&1

mkdir -p "$work/vanisher/usr/bin" "$work/taker/usr/bin"
printf 'vanisher\n' > "$work/vanisher/usr/bin/v"
printf 'taker\n' > "$work/taker/usr/bin/v"

scripts "$work/s-vanisher" vanisher
scripts "$work/s-taker" taker
make_pkg_scripts "$work/vanisher_1.0.aeltra" vanisher 1.0 "" "$work/vanisher" "$work/s-vanisher"
make_pkg_scripts "$work/taker_2.5.aeltra" taker 2.5 "Replaces: vanisher" "$work/taker" "$work/s-taker"

aept_run "$root" install --non-interactive "$work/vanisher_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing vanisher failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/taker_2.5.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "taking over vanisher's last file exited $rc:
$out"
installed vanisher && fail "vanisher did not disappear:
$out"

expected='taker preinst install
vanisher postrm disappear taker 2.5
taker postinst configure'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence around a disappearance:
$(cat "$log")"
note "install: preinst, the old package's postrm disappear, postinst"

# ── a package something depends on does not disappear ────────────────
#
# Owning no files is not enough on its own: a package another installed
# package depends on stays, empty, rather than leaving that dependency
# unsatisfied.  Its files are still taken; it just is not told it is
# gone.

mkdir -p "$work/needed/usr/bin" "$work/dependant/usr/bin" "$work/taker2/usr/bin"
printf 'needed\n' > "$work/needed/usr/bin/n"
printf 'dependant\n' > "$work/dependant/usr/bin/d"
printf 'taker2\n' > "$work/taker2/usr/bin/n"

scripts "$work/s-needed" needed
make_pkg_scripts "$work/needed_1.0.aeltra" needed 1.0 "" "$work/needed" "$work/s-needed"
make_pkg_tree "$work/dependant_1.0.aeltra" dependant 1.0 "Depends: needed" "$work/dependant"
make_pkg_tree "$work/taker2_1.0.aeltra" taker2 1.0 "Replaces: needed" "$work/taker2"

aept_run "$root" install --non-interactive "$work/needed_1.0.aeltra" \
    "$work/dependant_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing needed and dependant failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/taker2_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "taking over needed's last file exited $rc:
$out"
installed needed || fail "needed disappeared although dependant depends on it:
$out"
installed taker2 || fail "taker2 was not installed:
$out"
ran "needed postrm disappear taker2 1.0" \
    && fail "needed's postrm was told it disappeared, but it is still installed"
grep -q 'usr/bin/n' "$root/var/lib/aept/info/needed.list" \
    && fail "needed still claims the path taker2 took"
grep -q '^taker2$' "$root/usr/bin/n" || fail "the file was not taken over"
note "a package another package depends on stays installed, owning nothing"

# ── the unwind: a failed step is told to undo itself ─────────────────
#
# A prerm that stops a service relies on somebody starting it again if
# the upgrade never happens, and a preinst that made room relies on a
# postrm abort-* to give it back.  Each failure point has a defined
# sequence of calls after it, and the package must be left where it
# was: the old version installed, the new one not.

# A fresh install whose preinst fails: postrm abort-install, and nothing
# else -- the package is not there.
mkdir -p "$work/badpre/usr/bin"
printf 'badpre\n' > "$work/badpre/usr/bin/badpre"
scripts "$work/s-badpre" badpre preinst
make_pkg_scripts "$work/badpre_1.0.aeltra" badpre 1.0 "" "$work/badpre" "$work/s-badpre"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/badpre_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an install whose preinst failed exited 0:
$out"
installed badpre && fail "badpre is installed although its preinst failed"
expected='badpre preinst install
badpre postrm abort-install'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence after a failed preinst:
$(cat "$log")"
[ -e "$root/usr/bin/badpre" ] && fail "files were unpacked after the preinst failed"
note "install, preinst fails: postrm abort-install"

# A fresh install whose unpack fails, after the preinst has run: the
# same abort-install.  The data archive puts a file below a path that
# is a regular file on the system, which the clash check cannot see and
# extraction cannot do.
mkdir -p "$root/usr/lib"
printf 'in the way\n' > "$root/usr/lib/blocker"
mkdir -p "$work/badunpack/usr/lib/blocker"
printf 'x\n' > "$work/badunpack/usr/lib/blocker/inner"
scripts "$work/s-badunpack" badunpack
make_pkg_scripts "$work/badunpack_1.0.aeltra" badunpack 1.0 "" "$work/badunpack" \
    "$work/s-badunpack"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/badunpack_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an install whose unpack failed exited 0:
$out"
installed badunpack && fail "badunpack is installed although its unpack failed"
expected='badunpack preinst install
badunpack postrm abort-install'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence after a failed unpack:
$(cat "$log")"
note "install, unpack fails: postrm abort-install"
rm -f "$root/usr/lib/blocker"

# An upgrade whose new preinst fails: new postrm abort-upgrade, then the
# old postinst abort-upgrade so the old version can start again what
# its prerm stopped.  The old version stays.
mkdir -p "$work/svc1/usr/bin" "$work/svc2/usr/bin"
printf 'svc 1\n' > "$work/svc1/usr/bin/svc"
printf 'svc 2\n' > "$work/svc2/usr/bin/svc"
scripts "$work/s-svc1" svc-1.0
make_pkg_scripts "$work/svc_1.0.aeltra" svc 1.0 "" "$work/svc1" "$work/s-svc1"

scripts "$work/s-svc2-badpre" svc-2.0 preinst
make_pkg_scripts "$work/svc_2.0-badpre.aeltra" svc 2.0 "" "$work/svc2" "$work/s-svc2-badpre"

aept_run "$root" install --non-interactive "$work/svc_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing svc 1.0 failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/svc_2.0-badpre.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an upgrade whose preinst failed exited 0:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^svc - 1.0 ' \
    || fail "svc 1.0 is no longer the installed version"
grep -q '^svc 1$' "$root/usr/bin/svc" || fail "the old version's file was touched"
expected='svc-1.0 prerm upgrade 2.0
svc-2.0 preinst upgrade 1.0 2.0
svc-2.0 postrm abort-upgrade 1.0 2.0
svc-1.0 postinst abort-upgrade 2.0'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence after a failed upgrade preinst:
$(cat "$log")"
note "upgrade, new preinst fails: new postrm abort-upgrade, old postinst abort-upgrade"

# An upgrade whose old prerm fails: the new version's prerm is given a
# chance with failed-upgrade, and if it takes it the upgrade goes on.
mkdir -p "$work/svc3/usr/bin"
printf 'svc 3\n' > "$work/svc3/usr/bin/svc"
scripts "$work/s-svc2-badprerm" svc-2.0 prerm
make_pkg_scripts "$work/svc_2.0.aeltra" svc 2.0 "" "$work/svc2" "$work/s-svc2-badprerm"
scripts "$work/s-svc3" svc-3.0
make_pkg_scripts "$work/svc_3.0.aeltra" svc 3.0 "" "$work/svc3" "$work/s-svc3"

aept_run "$root" install --non-interactive "$work/svc_2.0.aeltra" >/dev/null 2>&1 \
    || fail "installing svc 2.0 failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/svc_3.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "an upgrade rescued by failed-upgrade exited $rc:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^svc - 3.0 ' \
    || fail "svc 3.0 is not the installed version"
expected='svc-2.0 prerm upgrade 3.0
svc-3.0 prerm failed-upgrade 2.0 3.0
svc-3.0 preinst upgrade 2.0 3.0
svc-2.0 postrm upgrade 3.0
svc-3.0 postinst configure 2.0'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence when the new prerm rescues the upgrade:
$(cat "$log")"
note "upgrade, old prerm fails: new prerm failed-upgrade, and the upgrade goes on"

# ... and if the new prerm cannot rescue it either, the old postinst
# is told, and the old version stays.
aept_run "$root" remove --non-interactive svc >/dev/null 2>&1
aept_run "$root" install --non-interactive "$work/svc_2.0.aeltra" >/dev/null 2>&1 \
    || fail "installing svc 2.0 again failed"
scripts "$work/s-svc3-badprerm" svc-3.0 prerm
make_pkg_scripts "$work/svc_3.0-badprerm.aeltra" svc 3.0 "" "$work/svc3" "$work/s-svc3-badprerm"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/svc_3.0-badprerm.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an upgrade whose prerms both failed exited 0:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^svc - 2.0 ' \
    || fail "svc 2.0 is no longer the installed version"
expected='svc-2.0 prerm upgrade 3.0
svc-3.0 prerm failed-upgrade 2.0 3.0
svc-2.0 postinst abort-upgrade 3.0'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence when neither prerm succeeds:
$(cat "$log")"
note "upgrade, both prerms fail: old postinst abort-upgrade"

# An upgrade whose old postrm fails: the new postrm is given
# failed-upgrade, and the upgrade completes either way -- the files are
# already the new version's.  (svc 2.0's prerm fails, so it cannot be
# removed directly; 3.0's prerm rescues the upgrade, and 3.0 can go.)
aept_run "$root" install --non-interactive "$work/svc_3.0.aeltra" >/dev/null 2>&1 \
    || fail "upgrading past the bad prerm failed"
aept_run "$root" remove --non-interactive svc >/dev/null 2>&1 \
    || fail "removing svc 3.0 failed"
scripts "$work/s-svc2-badpostrm" svc-2.0 postrm
make_pkg_scripts "$work/svc_2.0-badpostrm.aeltra" svc 2.0 "" "$work/svc2" \
    "$work/s-svc2-badpostrm"
aept_run "$root" install --non-interactive "$work/svc_2.0-badpostrm.aeltra" >/dev/null 2>&1 \
    || fail "installing svc 2.0 (bad postrm) failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/svc_3.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "an upgrade whose old postrm failed exited $rc:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^svc - 3.0 ' \
    || fail "svc 3.0 is not the installed version"
expected='svc-2.0 prerm upgrade 3.0
svc-3.0 preinst upgrade 2.0 3.0
svc-2.0 postrm upgrade 3.0
svc-3.0 postrm failed-upgrade 2.0 3.0
svc-3.0 postinst configure 2.0'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence after a failed old postrm:
$(cat "$log")"
note "upgrade, old postrm fails: new postrm failed-upgrade, and the upgrade completes"

# A removal whose prerm fails: postinst abort-remove, and the package
# stays with its files.
aept_run "$root" remove --non-interactive svc >/dev/null 2>&1
scripts "$work/s-svc2-badrm" svc-2.0 prerm
make_pkg_scripts "$work/svc_2.0-badrm.aeltra" svc 2.0 "" "$work/svc2" "$work/s-svc2-badrm"
aept_run "$root" install --non-interactive "$work/svc_2.0-badrm.aeltra" >/dev/null 2>&1 \
    || fail "installing svc 2.0 (bad prerm) failed"

: > "$log"
out=$(aept_run "$root" remove --non-interactive svc 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "a removal whose prerm failed exited 0:
$out"
installed svc || fail "svc was removed although its prerm failed"
grep -q '^svc 2$' "$root/usr/bin/svc" || fail "svc's file was deleted"
expected='svc-2.0 prerm remove
svc-2.0 postinst abort-remove'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence after a failed prerm remove:
$(cat "$log")"
note "remove, prerm fails: postinst abort-remove"

# ── a superseded package goes between unpack and configure ───────────
#
# Conflicts with Replaces: the old package must be gone before the new
# one is configured -- its daemon still holds the port, its alternatives
# are still registered -- and must still be there when the new one is
# unpacked, so its files can be taken over rather than vacated and
# refilled.  So its removal sits inside the new package's step: after
# the unpack, before the postinst.  Two MTAs are the textbook case.

mkdir -p "$work/mta1/usr/sbin" "$work/mta2/usr/sbin"
printf 'mta1\n' > "$work/mta1/usr/sbin/sendmail"
printf 'mta2\n' > "$work/mta2/usr/sbin/sendmail"
scripts "$work/s-mta1" mta1
scripts "$work/s-mta2" mta2
make_pkg_scripts "$work/mta1_1.0.aeltra" mta1 1.0 "Provides: mail-transport-agent
Conflicts: mail-transport-agent
Replaces: mail-transport-agent" "$work/mta1" "$work/s-mta1"
make_pkg_scripts "$work/mta2_1.0.aeltra" mta2 1.0 "Provides: mail-transport-agent
Conflicts: mail-transport-agent
Replaces: mail-transport-agent" "$work/mta2" "$work/s-mta2"

aept_run "$root" install --non-interactive "$work/mta1_1.0.aeltra" >/dev/null 2>&1 \
    || fail "installing mta1 failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/mta2_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing mta2 over mta1 exited $rc:
$out"
installed mta2 || fail "mta2 is not installed:
$out"
installed mta1 && fail "mta1 survived being superseded:
$out"
grep -q '^mta2$' "$root/usr/sbin/sendmail" || fail "the file was not taken over"
expected='mta2 preinst install
mta1 prerm remove
mta1 postrm remove
mta2 postinst configure'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence around a supersede:
$(cat "$log")"
note "supersede: new preinst, old prerm and postrm remove, new postinst"

# If the old package will not go -- its prerm fails -- the new one is
# left unpacked and not configured: configuring it beside the package
# it conflicts with is the thing the order exists to prevent.
mkdir -p "$work/mta3/usr/sbin"
printf 'mta3\n' > "$work/mta3/usr/sbin/sendmail"
scripts "$work/s-mta2-stuck" mta2 prerm
scripts "$work/s-mta3" mta3
make_pkg_scripts "$work/mta2_1.0-stuck.aeltra" mta2 1.0 "Provides: mail-transport-agent
Conflicts: mail-transport-agent
Replaces: mail-transport-agent" "$work/mta2" "$work/s-mta2-stuck"
make_pkg_scripts "$work/mta3_1.0.aeltra" mta3 1.0 "Provides: mail-transport-agent
Conflicts: mail-transport-agent
Replaces: mail-transport-agent" "$work/mta3" "$work/s-mta3"

aept_run "$root" remove --non-interactive mta2 >/dev/null 2>&1 || fail "removing mta2 failed"
aept_run "$root" install --non-interactive "$work/mta2_1.0-stuck.aeltra" >/dev/null 2>&1 \
    || fail "installing the stuck mta2 failed"

: > "$log"
out=$(aept_run "$root" install --non-interactive "$work/mta3_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "installing over a package that will not go exited 0:
$out"
ran "mta3 postinst configure" \
    && fail "mta3 was configured beside the package it conflicts with:
$(cat "$log")"
grep -q 'Status: install ok unpacked' "$root/var/lib/aept/info/mta3.control" \
    || fail "mta3 is not recorded as unpacked:
$(cat "$root/var/lib/aept/info/mta3.control" 2>&1)"
expected='mta3 preinst install
mta2 prerm remove
mta2 postinst abort-remove'
[ "$(cat "$log")" = "$expected" ] || fail "wrong sequence when the superseded package stays:
$(cat "$log")"
note "supersede, old prerm fails: the new package is left unpacked"

exit 0
