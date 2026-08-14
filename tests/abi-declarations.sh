#!/bin/sh
# abi-declarations.sh - print the declared ABI of libaept, one line per
# interface, sorted.
#
# Used by test_abi_symbols.sh to compare against tests/libaept.abi, and by
# "make abi-update" to regenerate that file.
#
# Usage: abi-declarations.sh <include-dir>     ($CC may override the compiler)
#
# The interface is taken from the headers rather than from the built
# library, because a symbol table carries names and nothing else.  A name
# list cannot see that aept_install() grew an argument, that a field was
# added to aept_pkg_info_t, or that the AEPT_FLAG_* enumerators were
# reordered -- and every one of those breaks callers that are not
# recompiled, silently, with the soname unchanged.
#
# Everything in the public headers is self-contained: aept_ctx_t is
# opaque, and every other type is defined in aept.h out of primitives.
# So the declared text is the whole ABI, and there is nothing here that
# only a tool reading DWARF could reach.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT

set -u

incdir=${1:?usage: abi-declarations.sh <include-dir>}
: "${CC:=cc}"

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

# msg.h and util.h pull in system headers, so the preprocessed output is
# filtered back down to lines that came from include/aept/ by their line
# markers.  AEPT_API is defined on the command line, which both suppresses
# the fallback definition in aept.h and leaves a distinctive token marking
# exactly what is exported.
printf '#include "aept/aept.h"\n#include "aept/msg.h"\n#include "aept/util.h"\n' \
    > "$tmp/abi.c"

$CC -E -I"$incdir" -DAEPT_API=AEPT_EXPORT "$tmp/abi.c" > "$tmp/pp" || exit 1

awk '
    # Line markers track which header the following lines came from.
    /^#[ \t]/ {
        f = $3
        gsub(/"/, "", f)
        keep = (f ~ /\/aept\/[^\/]*\.h$/)
        if (keep && !(f in seen)) {
            seen[f] = ++nfiles
            order[nfiles] = f
        }
        next
    }
    !keep { next }
    { buf[f] = buf[f] " " $0 }

    END {
        for (j = 1; j <= nfiles; j++)
            split_file(order[j], buf[order[j]])
        exit rc
    }

    # aept.h is the public header, so everything in it is ABI -- the
    # types as much as the functions.  The other aept headers are
    # internal and merely happen to carry three AEPT_API declarations
    # the CLI needs, so nothing unmarked in them is taken.
    function split_file(f, text,    i, c, depth, unit) {
        pub = (f ~ /\/aept\.h$/)
        # Split at each ";" that is not inside braces: a struct body has
        # semicolons of its own, and the whole body is one interface.
        # Nothing else in these headers produces a ";" -- there are no
        # function bodies and no "for" statements.
        depth = 0
        unit = ""
        for (i = 1; i <= length(text); i++) {
            c = substr(text, i, 1)
            if (c == "{")
                depth++
            else if (c == "}")
                depth--
            else if (c == ";" && depth == 0) {
                emit(unit)
                unit = ""
                continue
            }
            unit = unit c
        }
        if (unit ~ /[^ \t]/) {
            print "abi-declarations.sh: trailing garbage: " unit > "/dev/stderr"
            rc = 1
        }
    }

    function emit(u,    key) {
            gsub(/[ \t]+/, " ", u)
            sub(/^ /, "", u)
            sub(/ $/, "", u)
            if (u == "")
                return
            u = u ";"

            if (!pub && u !~ /AEPT_EXPORT/)
                return

            if (u ~ /AEPT_EXPORT/)
                key = "func:" fname(u)
            else if (u ~ /^typedef/)
                key = "type:" tname(u)
            else if (u ~ /^enum \{/)
                key = "enum:" ename(u)
            else {
                # Not a shape this knows how to key.  Fail rather than
                # skip: a silently dropped declaration is an interface
                # left unguarded.
                print "abi-declarations.sh: unrecognized declaration: " u \
                    > "/dev/stderr"
                rc = 1
                return
            }

            print key ": " u
    }

    # The identifier before the first "(" -- the parameter list.
    function fname(u,    head, m) {
        head = substr(u, 1, index(u, "(") - 1)
        while (match(head, /[A-Za-z_][A-Za-z0-9_]*[^A-Za-z0-9_]*$/)) {
            m = substr(head, RSTART)
            gsub(/[^A-Za-z0-9_]/, "", m)
            return m
        }
        return "?"
    }

    # A function-pointer typedef hides its name inside "(*name)"; anything
    # else names itself last, before the ";".
    function tname(u,    m) {
        if (match(u, /\(\*[A-Za-z_][A-Za-z0-9_]*\)/)) {
            m = substr(u, RSTART + 2, RLENGTH - 3)
            return m
        }
        if (match(u, /[A-Za-z_][A-Za-z0-9_]*;$/)) {
            m = substr(u, RSTART, RLENGTH - 1)
            return m
        }
        return "?"
    }

    # The enums are anonymous, so the first enumerator names them.  It is
    # as stable as the enum itself: changing it is an ABI change anyway.
    # One cosmetic consequence: reordering the enumerators moves the key,
    # so the test reports it as a removal plus an addition rather than as
    # a change.  The verdict is the same and it names the right enum.
    function ename(u,    m) {
        if (match(u, /\{ *[A-Za-z_][A-Za-z0-9_]*/)) {
            m = substr(u, RSTART, RLENGTH)
            sub(/^\{ */, "", m)
            return m
        }
        return "?"
    }
' "$tmp/pp" > "$tmp/keyed" || exit 1

LC_ALL=C sort "$tmp/keyed"
