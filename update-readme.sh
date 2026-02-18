#!/bin/sh -e

die() {
    echo "error: $1"
    exit 1
}

for TOOL in scdoc pandoc
do
    if ! which "$TOOL" >/dev/null 2>&1; then
        die "please install $TOOL"
    fi
done

scdoc < aept.1.scd | pandoc -f man -t gfm -o README.md
