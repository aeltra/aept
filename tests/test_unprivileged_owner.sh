#!/bin/sh
# test_unprivileged_owner.sh - installing as a user into a root that asks
# for ownership to be restored.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# A build box assembles a target root as an unprivileged user: the
# package's files say root, the user cannot make them so, and that is
# not a mistake -- but it is one aept has to be told about, with
# "option ignore_ownership 1", or a failed chown is the error it would
# be on a real root.  With the option the install goes through with the
# files the user's, the record says the user, and the setuid and setgid
# bits are dropped: a file that is not root's must not run as root.

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar sha256sum stat

[ "$(id -u)" -ne 0 ] || skip "runs as root; ownership can be set"

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

root=$work/root
new_root "$root"

# A package whose files are root's and one of which is setuid, as a
# real package's are.
mkdir -p "$work/t/usr/bin"
printf 'hello\n' > "$work/t/usr/bin/tool"
printf 'suid\n' > "$work/t/usr/bin/suid"
chmod 4755 "$work/t/usr/bin/suid"

d=$(mktemp -d)
mkdir -p "$d/c"
printf 'Package: own\nVersion: 1.0\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' > "$d/c/control"
tar czf "$d/control.tar.gz" -C "$d/c" control
tar czf "$d/data.tar.gz" --owner=0 --group=0 -C "$work/t" . || fail "tar --owner failed"
printf '2.0\n' > "$d/debian-binary"
( cd "$d" && ar rc "$work/own_1.0.aeltra" debian-binary control.tar.gz data.tar.gz ) || fail "ar failed"
rm -rf "$d"

# Without the option: refused, and the refusal names the option.
sed -i '/^option ignore_ownership 1$/d' "$root/etc/aept/aept.conf"
out=$(aept_run "$root" install --non-interactive "$work/own_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -ne 0 ] || fail "an unprivileged install without ignore_ownership went through:
$out"
printf '%s\n' "$out" | grep -q "cannot set the owner.*ignore_ownership" \
    || fail "the refusal does not name the option:
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^own ' && fail "own is recorded as installed"
note "without ignore_ownership, a chown that fails is an error naming the option"

echo 'option ignore_ownership 1' >> "$root/etc/aept/aept.conf"
rm -rf "$root/usr"
out=$(aept_run "$root" install --non-interactive "$work/own_1.0.aeltra" 2>&1)
rc=$?
[ "$rc" -eq 0 ] || fail "an unprivileged install with ignore_ownership failed (exit $rc):
$out"
aept_run "$root" list --installed 2>/dev/null | grep -q '^own ' || fail "own is not installed"
note "with it, the install goes through unprivileged"

me=$(id -u)
[ "$(stat -c %u "$root/usr/bin/tool")" = "$me" ] || fail "the file is not the user's"
grep -q "^\./usr/bin/tool	[0-9]*	$me	" "$root/var/lib/aept/info/own.list" \
    || fail "the record does not say the file is the user's:
$(grep 'usr/bin/tool' "$root/var/lib/aept/info/own.list")"
note "the files are the user's, and the record says so"

mode=$(stat -c %a "$root/usr/bin/suid")
[ "$mode" = "755" ] || fail "the setuid bit was kept on a file that is not root's (mode $mode)"
note "the setuid bit is dropped from a file the user owns"

# ── root inside a user namespace ─────────────────────────────────────
#
# A build box may run aept in a user namespace where it is uid 0: that
# is root enough to own a file as 0 (mapped to the user outside) but
# not to give one to an id the namespace does not map, where chown()
# fails however much aept looks like root.  With the option, a package
# file owned by 33 installs there, is 0's inside the namespace, and
# loses its setuid bit, while a file owned by 0 is owned as the package
# said and keeps it.  Without the option, the unmapped owner is the
# error it would be anywhere.

unshare -Urm true 2>/dev/null || { note "SKIP: no user namespaces for the namespaced case"; exit 0; }

mkdir -p "$work/u/usr/bin"
printf 'mine\n' > "$work/u/usr/bin/rootfile"
chmod 4755 "$work/u/usr/bin/rootfile"
printf 'www\n' > "$work/u/usr/bin/wwwfile"
chmod 4755 "$work/u/usr/bin/wwwfile"
d=$(mktemp -d)
mkdir -p "$d/c"
printf 'Package: ns\nVersion: 1.0\nArchitecture: all\nMaintainer: t <t@example.invalid>\nDescription: aept test fixture\n' > "$d/c/control"
tar czf "$d/control.tar.gz" --owner=0 --group=0 -C "$d/c" control
( cd "$work/u" && tar cf "$d/data.tar" --owner=0 --group=0 ./usr ./usr/bin ./usr/bin/rootfile \
    && tar rf "$d/data.tar" --owner=33 --group=33 ./usr/bin/wwwfile && gzip "$d/data.tar" ) \
    || fail "building the mixed-owner data archive failed"
printf '2.0\n' > "$d/debian-binary"
( cd "$d" && ar rc "$work/ns_1.0.aeltra" debian-binary control.tar.gz data.tar.gz ) || fail "ar failed"
rm -rf "$d"

root2=$work/root2
new_root "$root2"
sed -i '/^option ignore_ownership 1$/d' "$root2/etc/aept/aept.conf"
cat > "$work/inner.sh" <<INNER
"$AEPT_BIN" -o "$root2" -c "$root2/etc/aept/aept.conf" install --non-interactive "$work/ns_1.0.aeltra" > "$work/ns.out" 2>&1
echo "rc=\$?" > "$work/ns.result"
echo "root_owner=\$(stat -c %u "$root2/usr/bin/rootfile") root_mode=\$(stat -c %a "$root2/usr/bin/rootfile")" >> "$work/ns.result"
echo "www_owner=\$(stat -c %u "$root2/usr/bin/wwwfile") www_mode=\$(stat -c %a "$root2/usr/bin/wwwfile")" >> "$work/ns.result"
INNER
unshare -Urm sh "$work/inner.sh" || fail "the namespaced run failed"
grep -q '^rc=0$' "$work/ns.result" && fail "an unmapped owner was tolerated without ignore_ownership:
$(cat "$work/ns.out")"
grep -q "wwwfile.*cannot set the owner\|cannot set the owner.*wwwfile" "$work/ns.out" \
    || fail "the refusal does not name the unmapped file:
$(cat "$work/ns.out")"
note "root in a namespace without the option: an unmapped owner is refused"

echo 'option ignore_ownership 1' >> "$root2/etc/aept/aept.conf"
rm -rf "$root2/usr"
unshare -Urm sh "$work/inner.sh" || fail "the namespaced run failed"
grep -q '^rc=0$' "$work/ns.result" || fail "installing a package with an unmapped owner failed in the namespace:
$(cat "$work/ns.out")"
grep -q 'root_owner=0 root_mode=4755' "$work/ns.result" \
    || fail "the root-owned file is not root's inside the namespace with its setuid bit:
$(cat "$work/ns.result")"
grep -q 'www_owner=0 www_mode=755' "$work/ns.result" \
    || fail "the file with an unmapped owner is not the user's without its setuid bit:
$(cat "$work/ns.result")"
note "root in a namespace: mapped owners are set, unmapped ones tolerated, setuid dropped"

exit 0
