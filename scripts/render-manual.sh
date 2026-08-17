#!/bin/sh -e
# render-manual.sh - render aept.1.scd into browsable markdown.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Driven by "make docs".  The output lands in docs/aept.1.md, and the
# README links to it.
#
# This used to write README.md itself, which put NAME and SYNOPSIS at the
# top of the landing page: right in a terminal, where you arrived on
# purpose and want the reference, wrong on a page somebody reached from a
# search and is deciding whether to read.  The reference is still worth
# having in browsable form for anyone who has not installed the manpage,
# so it is still generated -- just not at the front door.
#
# The generated file is committed, so GitHub can render it without anyone
# needing scdoc and pandoc.

die() {
    echo "error: $1" >&2
    exit 1
}

cd "$(dirname "$0")/.." || die "cannot find the top of the tree"

for TOOL in scdoc pandoc; do
    command -v "$TOOL" >/dev/null 2>&1 || die "please install $TOOL"
done

[ -f aept.1.scd ] || die "aept.1.scd is missing"

mkdir -p docs

# The banner goes in as a markdown comment: invisible when rendered,
# unmissable to anyone who opens the file meaning to edit it.
{
    echo '<!-- Generated from aept.1.scd by scripts/render-manual.sh.'
    echo '     Do not edit: run "make docs" instead. -->'
    echo
    scdoc < aept.1.scd | pandoc -f man -t gfm
} > docs/aept.1.md

echo "wrote docs/aept.1.md"
