#!/bin/sh
# test_atomic_unpack.sh - a file is old or new, never half.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Every file is written aside as <path>.aept-new and renamed over its
# path once it is whole.  The case that shows the difference is a write
# that fails half-way: a package whose new file does not fit the
# filesystem.  With the file written in place, the old content is gone
# at the first byte and a truncated new one is left at the path; aside,
# the old file is untouched and the partial copy is removed.
#
# The filesystem that runs out is a tmpfs of a chosen size, mounted in
# an unprivileged mount namespace over the package's directory -- so
# the whole test runs inside that namespace, and skips where user
# namespaces are not available.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum unshare

unshare -Urm true 2>/dev/null || skip "unprivileged user and mount namespaces are not available"

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"
mkdir -p "$root/usr/lib/big"

# 1.0 ships a small file; 2.0 replaces it with one that will not fit.
mkdir -p "$work/v1/usr/lib/big" "$work/v2/usr/lib/big"
printf 'old\n' > "$work/v1/usr/lib/big/data"
printf 'old\n' > "$work/v1/usr/lib/big/other"
dd if=/dev/zero of="$work/v2/usr/lib/big/data" bs=1k count=400 2>/dev/null
printf 'new\n' > "$work/v2/usr/lib/big/other"
make_pkg_tree "$work/big_1.0.aeltra" big 1.0 "" "$work/v1"
make_pkg_tree "$work/big_2.0.aeltra" big 2.0 "" "$work/v2"

# Everything below runs inside one namespace, where the tmpfs is
# visible; results come out through $work/result.
cat > "$work/inner.sh" <<EOF
set -u
mount -t tmpfs -o size=128k tmpfs "$root/usr/lib/big" || { echo "mount failed"; exit 1; }
"$AEPT_BIN" -o "$root" -c "$root/etc/aept/aept.conf" install --non-interactive "$work/big_1.0.aeltra" >/dev/null 2>&1 \\
    || { echo "install 1.0 failed"; exit 1; }
"$AEPT_BIN" -o "$root" -c "$root/etc/aept/aept.conf" install --non-interactive "$work/big_2.0.aeltra" > "$work/out" 2>&1
echo "rc=\$?" > "$work/result"
echo "data=\$(cat "$root/usr/lib/big/data")" >> "$work/result"
echo "other=\$(cat "$root/usr/lib/big/other" 2>/dev/null)" >> "$work/result"
ls "$root/usr/lib/big" >> "$work/result"
EOF
unshare -Urm sh "$work/inner.sh" || fail "the namespaced run failed:
$(cat "$work/result" 2>/dev/null)"

grep -q '^rc=0$' "$work/result" && fail "an upgrade that ran out of space exited 0:
$(cat "$work/out")"
grep -q '^data=old$' "$work/result" \
    || fail "the file being replaced was damaged by the failed write:
$(cat "$work/result")
$(cat "$work/out")"
grep -q 'aept-new' "$work/result" && fail "a partial .aept-new was left behind:
$(cat "$work/result")"
note "a write that runs out of space leaves the old file intact and no partial copy"

# The upgrade was unwound: 1.0 is still the installed version.
grep -q '^Version: 1.0$' "$root/var/lib/aept/info/big.control" \
    || fail "the failed upgrade did not leave 1.0 installed:
$(cat "$root/var/lib/aept/info/big.control" 2>&1)"
note "and the old version stays recorded as installed"

exit 0
