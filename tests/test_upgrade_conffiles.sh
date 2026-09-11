#!/bin/sh
# test_upgrade_conffiles.sh - upgrading to a version that ships no
# conffiles must not leave the old version's conffile hashes behind.
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

info=$root/var/lib/aept/info

make_pkg_conffile "$work/app_1.0.aeltra" app 1.0 /etc/app.conf "one"
make_pkg          "$work/app_2.0.aeltra" app 2.0
make_pkg_conffile "$work/app_3.0.aeltra" app 3.0 /etc/app.conf "three"
# same conffile content as 1.0, higher version: the package did not
# touch the file, so an admin edit must win.
make_pkg_conffile "$work/app_1b.aeltra"  app 1.5 /etc/app.conf "one"

out=$(aept_run "$root" install --non-interactive "$work/app_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install of app 1.0 exited $rc:
$out"

[ -f "$info/app.conffiles" ] \
    || fail "app 1.0 shipped a conffile but no app.conffiles was recorded"
grep -q '/etc/app.conf' "$info/app.conffiles" \
    || fail "app.conffiles does not mention /etc/app.conf:
$(cat "$info/app.conffiles")"
note "a package with conffiles records its hashes"

# ── the regression ───────────────────────────────────────────────────
#
# app 2.0 ships no conffiles at all, so aept_conffile_resolve_upgrade()
# is never called and nothing rewrites the file.  The 1.0 hashes used to
# survive the upgrade and then influence conffile decisions for a later
# version that reintroduces the path.

out=$(aept_run "$root" install --non-interactive "$work/app_2.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade to app 2.0 exited $rc:
$out"

[ -f "$info/app.conffiles" ] \
    && fail "stale conffile hashes survived the upgrade to a version
that ships none:
$(cat "$info/app.conffiles")"
note "upgrading to a version without conffiles drops the stale hashes"

# ── the opposite direction ───────────────────────────────────────────
#
# The cleanup must not be done from remove_info_files(), which runs
# after the resolve step: that would delete the hashes the upgrade just
# recorded.

out=$(aept_run "$root" install --non-interactive "$work/app_3.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade to app 3.0 exited $rc:
$out"

[ -f "$info/app.conffiles" ] \
    || fail "upgrading to a version with conffiles recorded no hashes"
grep -q '/etc/app.conf' "$info/app.conffiles" \
    || fail "app.conffiles does not mention the reintroduced /etc/app.conf:
$(cat "$info/app.conffiles")"
note "upgrading to a version with conffiles keeps the fresh hashes"

# ── the decision matrix ──────────────────────────────────────────────
#
# Which way each arm falls is the whole contract of a conffile: whether
# the edit somebody made at 3am survives the next upgrade.  Only the
# "not on disk" arm and the interactive prompt had ever run.

conf=$root/etc/app.conf

fresh_install() {
    aept_run "$root" remove --non-interactive app >/dev/null 2>&1
    rm -f "$conf"
    aept_run "$root" install --non-interactive "$work/app_1.0.aeltra" >/dev/null 2>&1 \
        || fail "install of app 1.0 failed"
}

# The package changed, the admin did not: take the new file.
fresh_install
out=$(aept_run "$root" install --non-interactive "$work/app_3.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade over an untouched conffile exited $rc:
$out"
[ "$(cat "$conf")" = "three" ] \
    || fail "an untouched conffile was not updated: $(cat "$conf")"
note "an untouched conffile is replaced by the new version"

# The admin changed it and the package did not: keep the edit.
fresh_install
printf 'edited\n' > "$conf"
out=$(aept_run "$root" install --non-interactive "$work/app_1b.aeltra" 2>&1)
[ "$(cat "$conf")" = "edited" ] \
    || fail "an edit was lost when the package shipped the same file: $(cat "$conf")"
note "an edited conffile survives when the package did not change it"

# Both arrived at the same content: nothing to do, and no prompt.
fresh_install
printf 'three\n' > "$conf"
out=$(aept_run "$root" install --non-interactive "$work/app_3.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "upgrade to an identical conffile exited $rc:
$out"
[ "$(cat "$conf")" = "three" ] || fail "identical content was disturbed: $(cat "$conf")"
note "a conffile already matching the new version is left alone"

# ── a conffile path that tries to escape ─────────────────────────────
#
# The list comes out of the package, so it is attacker-chosen: a path
# climbing out of the root must be refused by name, not resolved.
make_pkg_conffile "$work/evil_1.0.aeltra" evil 1.0 /etc/evil.conf "x"
evil_dir=$(mktemp -d) || fail "mktemp failed"
( cd "$evil_dir" && ar x "$work/evil_1.0.aeltra" && mkdir -p c \
    && tar xzf control.tar.gz -C c \
    && printf '../../../etc/shadow\n/etc/evil.conf\n' > c/conffiles \
    && tar czf control.tar.gz -C c control conffiles \
    && rm -f "$work/evil_1.0.aeltra" \
    && ar rc "$work/evil_1.0.aeltra" debian-binary control.tar.gz data.tar.gz ) \
    >/dev/null 2>&1 || fail "rebuilding the crafted package failed"
rm -rf "$evil_dir"

out=$(aept_run "$root" install --non-interactive "$work/evil_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "installing the crafted package exited $rc:
$out"
case $out in
    *"unsafe conffile path"*) ;;
    *) fail "an escaping conffile path drew no warning:
$out" ;;
esac
grep -q 'shadow' "$info/evil.conffiles" 2>/dev/null \
    && fail "the escaping path was recorded as a conffile:
$(cat "$info/evil.conffiles")"
note "a conffile path that climbs out of the root is refused"

exit 0
