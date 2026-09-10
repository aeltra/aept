#!/bin/sh -e
# musl-build.sh - build and test aept against musl libc, in a container.
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Standalone on purpose: nothing in the build system calls this, and it
# writes nothing into the tree.  Run it by hand after touching code whose
# behaviour could differ between libcs.
#
# Alpine is the musl distribution here, and it packages no libsolv at all,
# so the image builds one from source.  That is the slow part, which is
# why the image is built once and reused; --rebuild forces it again.
#
# The source goes in through "git ls-files": exactly the tracked and
# untracked-but-not-ignored files, so no build artifact rides along and no
# --exclude list has to be kept in step.  The host tree is only read, so a
# musl run cannot disturb the working copy's configure state.
#
# usign is not packaged for Alpine, so the signature tests skip rather
# than run.  What this covers is the C: the library, the unit tests, and
# the shell tests that need no missing tool.

IMAGE=aept-musl-build
ALPINE_VERSION=3.24
LIBSOLV_REF=${LIBSOLV_REF:-0.7.39}

die() {
    echo "error: $1" >&2
    exit 1
}

usage() {
    cat <<EOF
usage: $0 [--rebuild] [--shell] [-- CONFIGURE_ARGS...]

  --rebuild   rebuild the container image even if it exists
  --shell     drop into a shell in the built tree instead of running the suite
  --          pass the remaining arguments to configure
EOF
}

REBUILD=no
SHELL_MODE=no

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild) REBUILD=yes; shift ;;
        --shell)   SHELL_MODE=yes; shift ;;
        -h|--help) usage; exit 0 ;;
        --)        shift; break ;;
        *)         usage >&2; die "unknown option: $1" ;;
    esac
done

command -v docker >/dev/null 2>&1 || die "docker is not installed"
docker info >/dev/null 2>&1 || die "cannot talk to the docker daemon"

cd "$(dirname "$0")/.." || die "cannot find the top of the tree"
git rev-parse --git-dir >/dev/null 2>&1 || die "this needs a git checkout"

if [ "$REBUILD" = yes ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "building $IMAGE (alpine $ALPINE_VERSION, libsolv $LIBSOLV_REF)..."
    # Fed on stdin so the build context is empty: the image fetches
    # everything it needs, and there is nothing here to send.
    docker build -t "$IMAGE" \
        --build-arg "ALPINE_VERSION=$ALPINE_VERSION" \
        --build-arg "LIBSOLV_REF=$LIBSOLV_REF" - <<'EOF' || die "image build failed"
ARG ALPINE_VERSION
FROM alpine:${ALPINE_VERSION}
ARG LIBSOLV_REF
RUN apk add --no-cache \
        build-base autoconf automake libtool pkgconf \
        libarchive-dev openssl-dev zlib-dev xz-dev bzip2-dev \
        python3 dash diffutils git cmake
# ENABLE_DEBIAN is not optional: it provides repo_add_debpackages(),
# which is how solver.c loads an index.
RUN git clone --depth 1 --branch "$LIBSOLV_REF" \
        https://github.com/openSUSE/libsolv /tmp/libsolv \
 && cmake -S /tmp/libsolv -B /tmp/libsolv/build \
        -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_DEBIAN=ON -DMULTI_SEMANTICS=ON \
 && cmake --build /tmp/libsolv/build -j"$(nproc)" \
 && cmake --install /tmp/libsolv/build \
 && rm -rf /tmp/libsolv
EOF
fi

# The source is mounted as a tarball rather than piped in, so that stdin
# stays free -- --shell needs it for the terminal.
TARBALL=$(mktemp) || die "mktemp failed"
trap 'rm -f "$TARBALL"' EXIT INT TERM
git ls-files -z --cached --others --exclude-standard \
    | tar --null -T - -cf "$TARBALL"

# Everything from here runs under busybox ash: POSIX only, no arrays, no
# PIPESTATUS, no [[ ]].
BUILD='
set -e
mkdir -p /work
tar -C /work -xf /src.tar
cd /work
autoreconf -i >/dev/null
mkdir -p build
cd build
../configure $CONFIGURE_ARGS >/tmp/configure.log 2>&1 || {
    tail -30 /tmp/configure.log
    echo "configure failed" >&2
    exit 1
}
make -j"$(nproc)" >/tmp/make.log 2>&1 || {
    grep -E "error:" /tmp/make.log | head -30
    echo "build failed" >&2
    exit 1
}
warnings=$(grep -c "warning:" /tmp/make.log || true)
echo "built against musl: $warnings warnings"
[ "$warnings" = 0 ] || grep "warning:" /tmp/make.log | head -20
'

CHECK='
if make check >/tmp/check.log 2>&1; then rc=0; else rc=$?; fi
grep -E "^# (TOTAL|PASS|SKIP|FAIL|ERROR):" /tmp/check.log || true
skipped=$(grep -E "^SKIP:" /tmp/check.log || true)
[ -z "$skipped" ] || { echo; echo "$skipped"; }
failed=$(grep -E "^(FAIL|ERROR):" /tmp/check.log || true)
if [ -n "$failed" ]; then
    echo
    echo "$failed"
    for t in $(echo "$failed" | sed -e "s/^[A-Z]*: //" -e "s/\.sh$//"); do
        echo "--- tests/$t.log ---"
        tail -25 "tests/$t.log" 2>/dev/null || true
    done
fi
exit $rc
'

SHELL_IN='
echo
echo "--- musl shell; the build is in /work/build.  ^D to leave. ---"
exec sh
'

if [ "$SHELL_MODE" = yes ]; then
    [ -t 0 ] || die "--shell needs a terminal on stdin"
    RUN_FLAGS="-it"
    SCRIPT="$BUILD$SHELL_IN"
else
    RUN_FLAGS="-i"
    SCRIPT="$BUILD$CHECK"
fi

exec docker run --rm $RUN_FLAGS \
    -v "$TARBALL:/src.tar:ro" \
    -e "CONFIGURE_ARGS=$*" \
    "$IMAGE" sh -c "$SCRIPT"
