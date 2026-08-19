#!/bin/sh
# test_python.sh - the cffi bindings against the built libaept.so:
# nothing verifies that _cdef.py still matches aept.h except a consumer
# calling through it, so this is that consumer -- plus a drift check
# holding _cdef.py against the ABI baseline the C side already guards.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

. "${srcdir:-.}/aeptlib.sh"

require_aept
require_tools ar tar python3
python3 -c 'import cffi' 2>/dev/null || skip "python3-cffi is not available"
[ -n "${LIBAEPT_SO:-}" ] && [ -e "$LIBAEPT_SO" ] || skip "libaept.so is not built"

work=$(mktemp -d) || fail "mktemp failed"
trap 'rm -rf "$work"' EXIT

# The repo's aeltra/ is a *namespace* package.  A machine with the
# Aeltra tooling installed has a *regular* aeltra package in
# dist-packages, and Python lets a regular package shadow every
# namespace portion regardless of sys.path order -- so PYTHONPATH
# alone would silently test the installed bindings, not these.  A
# regular-package shim with the repo's aept copied in wins on path
# order against the installed one.
shim=$work/shim
mkdir -p "$shim/aeltra"
: > "$shim/aeltra/__init__.py"
cp -a "$srcdir/../python/aeltra/aept" "$shim/aeltra/aept"
export PYTHONDONTWRITEBYTECODE=1

root=$work/root
new_root "$root"
make_pkg "$work/pyfix_1.0.aeltra" pyfix 1.0

# ── the bindings drive a real install-query-remove cycle ─────────────

LIBAEPT_PATH=$LIBAEPT_SO PYTHONPATH=$shim \
python3 - "$root" "$work/pyfix_1.0.aeltra" <<'PYEOF'
import sys

from aeltra.aept import Aept, AeptError, Flag

root, pkg = sys.argv[1], sys.argv[2]
seen_logs = []

with Aept() as a:
    a.set_offline_root(root)
    a.load_config(root + "/etc/aept/aept.conf")
    a.set_log_callback(lambda level, msg: seen_logs.append(msg))
    a.set_flag(Flag.NON_INTERACTIVE, True)

    archs = a.architectures()
    assert archs == ["all"], f"architectures: {archs}"

    ok = a.install(local_paths=[pkg])
    assert ok is True, "install() should report clean triggers"

    pkgs = a.list_packages(installed=True)
    assert [p.name for p in pkgs] == ["pyfix"], f"list: {pkgs}"
    assert pkgs[0].version == "1.0"
    assert pkgs[0].installed

    info = a.show("pyfix")
    assert info is not None and info.name == "pyfix" and info.version == "1.0", f"show: {info}"
    assert a.show("absent") is None, "show(absent) should be None"

    files = a.files("pyfix")
    assert files and any(f.endswith("usr/bin/pyfix") for f in files), f"files: {files}"
    assert a.files("absent") is None, "files(absent) should be None"

    owners = a.owns("/usr/bin/pyfix")
    assert owners == ["pyfix"], f"owns: {owners}"
    assert a.owns("/nowhere") is None, "owns(nowhere) should be None"

    a.triggers()  # nothing pending: a quiet no-op

    # Removing what is not installed resolves to an empty transaction:
    # a no-op, not an error -- pinned as observed.
    a.remove(["nonexistent-package"])

    # An actual failure must surface as AeptError: installing a name
    # nothing provides is a solver problem, not a no-op.
    try:
        a.install(names=["no-such-package"])
    except AeptError:
        pass
    else:
        raise SystemExit("installing an unresolvable name did not raise")

    a.remove(["pyfix"])
    assert a.list_packages(installed=True) == [], "pyfix survived remove()"

assert seen_logs, "the log callback never received anything"
print("bindings-roundtrip-ok")
PYEOF
[ $? -eq 0 ] || fail "the bindings round trip failed"
note "install, query and remove all work through the bindings"

# ── _cdef.py must not drift from the ABI baseline ────────────────────
#
# The C side guards declarations with tests/libaept.abi; the bindings
# re-state them by hand in _cdef.py.  Every exported function must be
# declared to cffi, and cffi must declare nothing the library lacks --
# either direction of drift strands a consumer at call time.

python3 - "$srcdir/libaept.abi" "$srcdir/../python/aeltra/aept/_cdef.py" <<'PYEOF'
import re
import sys

abi = open(sys.argv[1]).read()
cdef = open(sys.argv[2]).read()

abi_fns = set(re.findall(r'\b(aept_\w+)\s*\(', abi))
cdef_fns = set(re.findall(r'\b(aept_\w+)\s*\(', cdef))

# Exported for the CLI, not for embedders: the CLI links libaept like
# any consumer and needs the logger and the OOM-safe allocators.  Two
# are varargs, which cffi could not call usefully anyway.  Everything
# else the library exports, the bindings must declare.
cli_only = {"aept_log", "aept_malloc", "aept_asprintf"}

missing = abi_fns - cdef_fns - cli_only
extra = cdef_fns - abi_fns
if missing:
    raise SystemExit(f"exported but absent from _cdef.py: {sorted(missing)}")
if extra:
    raise SystemExit(f"declared to cffi but not exported: {sorted(extra)}")
print(f"cdef-abi-agree-ok ({len(abi_fns)} functions)")
PYEOF
[ $? -eq 0 ] || fail "_cdef.py has drifted from the ABI baseline"
note "_cdef.py and the ABI baseline agree on the exported set"

exit 0
