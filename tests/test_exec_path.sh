#!/bin/sh
# test_exec_path.sh - aept must not let the environment choose the
# helpers it execs, neither through PATH nor through $SHELL.  It
# normally runs as root, so whoever picks the binary picks what runs
# with those privileges.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum python3

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

# A directory of impostors, one per helper aept execs.  Each records
# that it ran and then does nothing, so a hijack is visible as a marker
# file rather than as a change in behaviour.
evil=$work/evil
mkdir -p "$evil"

for tool in rm diff sh from_env; do
    printf '#!/bin/sh\n: > "%s/hijacked.%s"\nexit 0\n' "$work" "$tool" \
        > "$evil/$tool"
    chmod 755 "$evil/$tool"
done

assert_not_hijacked() {
    if [ -e "$work/hijacked.$1" ]; then
        fail "$2: the impostor '$1' was executed"
    fi
    note "$2: the impostor '$1' was not executed"
}

# ── rm, during ordinary install cleanup ──────────────────────────────
#
# Both install and upgrade clean their control scratch directory with
# "rm -rf".  Neither is interactive, so PATH alone drives this.

root=$work/root
new_root "$root"

make_pkg "$work/app_1.0.aeltra" app 1.0

out=$(PATH="$evil:$PATH" aept_run "$root" install --non-interactive \
        "$work/app_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "install exited $rc:
$out"

assert_not_hijacked rm "install"

# The real rm must still have run: a fix that merely broke cleanup would
# pass the check above.
leftover=$(find "$root/tmp" -maxdepth 1 -name 'aept-*' 2>/dev/null | wc -l)
[ "$leftover" -eq 0 ] \
    || fail "install left $leftover control scratch directories behind"
note "install: the control scratch directory was cleaned up"

# ── diff and sh, behind the conffile prompt ──────────────────────────
#
# The D and Z options of the conffile prompt exec diff and a shell.  The
# prompt only appears on a tty and only when both sides of a conffile
# changed, so this needs a pty and a package whose conffile the user has
# edited.

setup_conffile_conflict() {
    rm -rf "$root"
    new_root "$root"

    make_pkg_conffile "$work/cf_1.0.aeltra" cf 1.0 /etc/cf.conf "shipped by 1.0"
    make_pkg_conffile "$work/cf_2.0.aeltra" cf 2.0 /etc/cf.conf "shipped by 2.0"

    out=$(aept_run "$root" install --non-interactive "$work/cf_1.0.aeltra" 2>&1)
    [ $? -eq 0 ] || fail "conffile setup install failed:
$out"

    # The user edits it, so neither side matches and aept must ask.
    printf 'edited by the user\n' > "$root/etc/cf.conf"
}

# The Z option used to exec $SHELL, so the environment — not just PATH
# — chose the binary.  Point SHELL at an impostor for every run.
drive() {
    PATH="$evil:$PATH" SHELL="$evil/from_env" \
        python3 "${srcdir:-.}/ptydrive.py" "$1" -- \
        "$AEPT_BIN" -o "$root" -c "$root/etc/aept/aept.conf" \
        install "$work/cf_2.0.aeltra" 2>&1
}

# D shows the differences.
setup_conffile_conflict
out=$(drive "D,N")
rc=$?
[ "$rc" -ne 2 ] || fail "the conffile prompt never answered:
$out"

printf '%s\n' "$out" | grep -q 'What would you like to do about it' \
    || skip "the conffile prompt did not appear; cannot test D/Z:
$out"
note "conffile prompt reached over a pty"

assert_not_hijacked diff "conffile D"

# Z starts a shell to examine the situation.
setup_conffile_conflict
out=$(drive "Z,N")
rc=$?
[ "$rc" -ne 2 ] || fail "the conffile prompt never answered:
$out"

assert_not_hijacked from_env "conffile Z (\$SHELL)"
assert_not_hijacked sh "conffile Z (PATH)"

# ── the user's choice still took effect ──────────────────────────────
#
# Answering N keeps the installed version.  Checking it guards against a
# fix that silences the prompt instead of hardening it.

grep -q 'edited by the user' "$root/etc/cf.conf" \
    || fail "answering N did not keep the user's version:
$(cat "$root/etc/cf.conf")"
note "answering N kept the user's version"

exit 0
