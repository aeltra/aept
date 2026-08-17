#!/bin/sh
# test_abi_symbols.sh - the ABI matches the committed baseline.
#
# debian/rules already guards that the version numbers agree with each
# other and that the soname major matches the binary package name.  What
# none of that covers is whether the library kept the promise those
# numbers make: the soname stays libaept.so.0, installed binaries load
# the new library, and they die at run time instead of being refused at
# load time, which is the whole point of soname versioning.
#
# The three ways that happens are not equally bad, and this test does not
# treat them alike:
#
#   removed    an interface is gone.  Callers fail to resolve it.  FAIL.
#   changed    same name, different signature, struct layout or
#              enumerator order.  Worse than removed, because it resolves
#              and then misbehaves.  FAIL.
#   added      a new interface.  Breaks nobody, so it PASSES; it needs a
#              minor bump before release, which the note says.
#
# "changed" is why the baseline records whole declarations rather than
# names: a symbol table has no signatures in it, so a name list would
# call the worst of the three a pass.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

# A coverage build cannot answer this question.  libgcov's runtime
# (__gcov_dump, __gcov_master, __gcov_var, ...) and mangle_path come out
# of libgcov.a, which is not compiled with -fvisibility=hidden, so they
# reach the dynamic symbol table and the "exported set is exactly what
# AEPT_API marks" rule is genuinely violated.  That rule is correct and
# the violation is real, so this skips rather than being taught to ignore
# a class of symbol -- an exception carried here would also hide a real
# internal that escaped.  Every non-coverage build still checks it.
[ "${AEPT_COVERAGE:-0}" = 1 ] && skip "coverage build: libgcov exports symbols of its own"

blessed=${srcdir:-.}/libaept.abi
extract=${srcdir:-.}/abi-declarations.sh
incdir=${ABI_INCLUDEDIR:-${srcdir:-.}/../include}

[ -f "$blessed" ] || fail "$blessed is missing"
[ -f "$extract" ] || fail "$extract is missing"

# NM comes from libtool's LT_PATH_NM via AM_TESTS_ENVIRONMENT, so it is
# the host-prefixed one on a cross build and carries libtool's own flags
# (here "nm -B").  It is left unquoted for that reason.  No configure
# check of our own: libtool cannot build a shared library without nm, so
# it is already a hard build dependency -- which also means this skip
# cannot fire while there is a shared library to check, so the guard is
# not quietly skippable.  Debian needs no Build-Depends line either:
# binutils comes with build-essential.
[ -n "${NM:-}" ] && [ "${NM}" != "false" ] || skip "no nm available"

# The one reachable skip: --disable-shared, where there is no ABI to
# guard because nothing links against libaept at run time.
[ -n "${LIBAEPT_SO:-}" ] && [ -f "$LIBAEPT_SO" ] \
    || skip "no shared libaept was built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

rc=0

# ── what the headers declare, and what the library exports ───────────

sh "$extract" "$incdir" > "$work/declared" 2>"$work/err" \
    || fail "could not read the declared ABI:
$(cat "$work/err")"
[ -s "$work/declared" ] || fail "the declared ABI came out empty"

grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$' "$blessed" \
    | LC_ALL=C sort > "$work/blessed"
[ -s "$work/blessed" ] || fail "$blessed lists no interfaces"

$NM -D --defined-only "$LIBAEPT_SO" > "$work/nm.out" 2>"$work/nm.err" \
    || skip "nm failed on $LIBAEPT_SO: $(cat "$work/nm.err")"

# Only the linker's own boundary symbols are dropped, by name: they are
# not part of aept's promise and they vary between toolchains.  The list
# is deliberately not filtered to aept_* -- filtering by prefix is what
# let src/libfetch/ stay exported unnoticed under the old
# -export-symbols-regex.
awk '{ print $NF }' "$work/nm.out" \
    | grep -v -x -e _init -e _fini -e __bss_start -e _edata -e _end \
    | LC_ALL=C sort -u > "$work/exported"

sed -n 's/^func:\([^:]*\):.*/\1/p' "$work/declared" | LC_ALL=C sort -u \
    > "$work/declared_funcs"

# ── the library exports exactly what AEPT_API marks ──────────────────
#
# This holds regardless of the baseline: a leaked internal and a declared
# function that never got compiled in are both bugs, not ABI decisions.

if ! diff -u "$work/declared_funcs" "$work/exported" > "$work/d1"; then
    echo "what libaept exports is not what AEPT_API marks in $incdir:"
    echo "  '-' declared but not exported, '+' exported but not declared"
    echo "  (a '+' is an internal symbol that escaped -fvisibility=hidden)"
    sed 1,2d "$work/d1"
    rc=1
else
    note "$(wc -l < "$work/exported" | tr -d ' ') exported symbols, each one an AEPT_API declaration"
fi

# Everything AEPT_API marks is a function, so a "T" on every line.  Any
# other type means data crossed the boundary -- a mutable global, which
# is a thread-safety problem too, or a const whose size gets pinned into
# every caller.  Either way it should be a deliberate act, not a slip.
if awk '$2 != "T" { print }' "$work/nm.out" | grep -q .; then
    echo "libaept exports something that is not a function:"
    awk '$2 != "T" { print "  " $0 }' "$work/nm.out"
    rc=1
fi

# Every named type an exported signature mentions must be recorded here,
# or its layout would be ABI that nothing guards.  Matched on the _t and
# _fn suffixes every aept type carries; "struct aept_ctx" is spelled
# aept_ctx_t everywhere and is opaque in any case.  Function names are
# subtracted, because aept_set_log_fn() and friends end in _fn too.
sed -n 's/^type:\([^:]*\):.*/\1/p' "$work/declared" > "$work/known"
cat "$work/declared_funcs" >> "$work/known"
LC_ALL=C sort -u -o "$work/known" "$work/known"
sed -n 's/^func:[^:]*: //p' "$work/declared" \
    | grep -oE 'aept_[A-Za-z0-9_]*(_t|_fn)\b' | LC_ALL=C sort -u \
    > "$work/used_types"
comm -13 "$work/known" "$work/used_types" > "$work/d2"
if [ -s "$work/d2" ]; then
    echo "an exported signature uses a type that the baseline does not record:"
    sed 's/^/  /' "$work/d2"
    echo "  (declare it in aept.h, or its layout is ABI with nothing guarding it)"
    rc=1
fi

# ── the declared ABI against the committed baseline ──────────────────

cut -d: -f1,2 "$work/blessed"  | LC_ALL=C sort > "$work/blessed_keys"
cut -d: -f1,2 "$work/declared" | LC_ALL=C sort > "$work/declared_keys"

comm -23 "$work/blessed_keys" "$work/declared_keys" > "$work/removed"
comm -13 "$work/blessed_keys" "$work/declared_keys" > "$work/added"

# Present in both, but not identical.
LC_ALL=C comm -12 "$work/blessed_keys" "$work/declared_keys" > "$work/common"
: > "$work/changed"
line_for() { awk -v k="$2: " 'index($0, k) == 1 { print; exit }' "$1"; }
while IFS= read -r k; do
    b=$(line_for "$work/blessed" "$k")
    d=$(line_for "$work/declared" "$k")
    [ "$b" = "$d" ] || printf '%s\n  was: %s\n  now: %s\n' \
        "$k" "${b#*: }" "${d#*: }" >> "$work/changed"
done < "$work/common"

if [ -s "$work/removed" ]; then
    echo "interfaces removed since tests/libaept.abi was blessed:"
    sed 's/^/  /' "$work/removed"
    rc=1
fi

if [ -s "$work/changed" ]; then
    echo "interfaces changed since tests/libaept.abi was blessed:"
    sed 's/^/  /' "$work/changed"
    rc=1
fi

if [ -s "$work/removed" ] || [ -s "$work/changed" ]; then
    echo "  If the difference is real -- a type, a struct field, an"
    echo "  enumerator value, an interface gone -- it breaks callers that"
    echo "  are not recompiled: bump major in src/Makefile.am's"
    echo "  -version-number (minor = revision = 0), then 'make abi-update'."
    echo "  If it is only cosmetic, such as a renamed parameter, the"
    echo "  declaration text still changed: 'make abi-update' alone."
fi

if [ -s "$work/added" ]; then
    # Not a failure: an addition breaks no existing caller.
    note "interfaces added since tests/libaept.abi was blessed:"
    sed 's/^/#   /' "$work/added"
    note "  these need minor++ (revision = 0) before release, and"
    note "  'make abi-update' so they are guarded from then on"
fi

[ "$rc" -eq 0 ] || exit 1

[ -s "$work/added" ] \
    || note "$(wc -l < "$work/blessed" | tr -d ' ') interfaces, unchanged from the baseline"

exit 0
