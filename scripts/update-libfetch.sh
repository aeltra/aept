#!/bin/sh
# Update vendored libfetch from Alpine's apk-tools repository.
set -e

REPO_URL="https://gitlab.alpinelinux.org/alpine/apk-tools.git"
DEST="libfetch"
PATCHDIR="patches/libfetch"

# Run from the repo root regardless of caller's cwd.
cd "$(dirname "$0")/.."

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

echo "Cloning apk-tools (shallow)..."
git clone --depth 1 "$REPO_URL" "$tmpdir/apk-tools"

if [ ! -d "$tmpdir/apk-tools/$DEST" ]; then
    echo "Error: $DEST not found in apk-tools" >&2
    exit 1
fi

echo "Replacing local $DEST/..."
rm -rf "$DEST"
cp -a "$tmpdir/apk-tools/$DEST" "$DEST"

# Each patch fixes one upstream bug and is written to be submittable to
# apk-tools as-is; they are applied in numeric order.
for patchfile in "$PATCHDIR"/[0-9]*.patch; do
    echo "Applying $(basename "$patchfile")..."
    patch -p1 --forward --no-backup-if-mismatch < "$patchfile"
done

if find "$DEST" -name '*.orig' -o -name '*.rej' | grep -q .; then
    echo "Error: patch left .orig/.rej files in $DEST" >&2
    exit 1
fi

echo "Done. Updated $DEST from apk-tools master and applied local fixes."
