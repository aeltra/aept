#!/bin/sh
# test_script_confinement.sh - a maintainer script run in an offline
# root stays inside it.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# A script is chrooted into the offline root, but a chroot alone does
# not hold it there:
#
#   - A directory descriptor aept inherited from its caller survives
#     chroot(), and a path relative to it leads back to the host's tree.
#     aept closes those before it runs a script, in every offline root.
#
#   - Run unprivileged, aept puts the script into a user namespace of
#     its own and makes it root there.  That root holds CAP_SYS_CHROOT,
#     with which a second chroot() leaves the first, and CAP_SYS_ADMIN,
#     with which it can make namespaces of its own and mount there.
#     aept drops both before it runs the script.  As real root it does
#     not: real root has other ways out, and the drop would only break
#     scripts that need either capability.
#
# The script runs scriptprobe, which tries each of these and reports.
# Under the package build the suite runs as root, so the capability
# half is noted as skipped there.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum

[ -n "${SCRIPTPROBE:-}" ] && [ -x "$SCRIPTPROBE" ] || skip "scriptprobe is not built; run make check"

if [ "$(id -u)" -ne 0 ]; then
    unshare -Ur true 2>/dev/null || skip "user namespaces are not available"
fi

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

mkdir "$work/outside"
echo "host secret" > "$work/outside/secret"

root=$work/root
new_root "$root"
provision_shell "$root" || skip "cannot provision a shell into the root"
cp "$SCRIPTPROBE" "$root/bin/scriptprobe" || fail "cannot copy scriptprobe into the root"

make_pkg_script "$work/p_1.0.aeltra" p 1.0 postinst \
'/bin/scriptprobe read 9 secret > /probe.out 2>&1
/bin/scriptprobe chroot >> /probe.out 2>&1
/bin/scriptprobe unshare >> /probe.out 2>&1
exit 0'

# ── control: the descriptor does lead outside without aept ───────────

out=$("$SCRIPTPROBE" read 9 secret 9<"$work/outside")
[ "$out" = "host secret" ] || fail "scriptprobe cannot read through a descriptor: '$out'"

# ── the install, with a directory descriptor open as 9 ───────────────

out=$(aept_run "$root" install --non-interactive "$work/p_1.0.aeltra" 9<"$work/outside" 2>&1) \
    || fail "install failed:
$out"
[ -f "$root/probe.out" ] || fail "the postinst did not run:
$out"

probe=$(cat "$root/probe.out")

case $probe in
    *"host secret"*) fail "the script read outside the root through an inherited descriptor:
$probe" ;;
esac
printf '%s\n' "$probe" | grep -q '^read: Bad file descriptor$' \
    || fail "the descriptor did not arrive closed:
$probe"
note "the script did not inherit the directory descriptor"

if [ "$(id -u)" -eq 0 ]; then
    note "SKIP: runs as root; aept does not drop capabilities for real root"
    exit 0
fi

printf '%s\n' "$probe" | grep -q '^chroot: Operation not permitted$' \
    || fail "the script could chroot:
$probe"
note "the script cannot chroot"

printf '%s\n' "$probe" | grep -q '^unshare: Operation not permitted$' \
    || fail "the script could make a mount namespace:
$probe"
note "the script cannot make a mount namespace"

exit 0
