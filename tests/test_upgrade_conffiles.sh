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

make_aeltra_conffile "$work/app_1.0.aeltra" app 1.0 /etc/app.conf "one"
make_aeltra          "$work/app_2.0.aeltra" app 2.0
make_aeltra_conffile "$work/app_3.0.aeltra" app 3.0 /etc/app.conf "three"

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

exit 0
