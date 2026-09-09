AC_INIT([aept], [0.4.1])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])
AC_CONFIG_MACRO_DIRS([m4])

AC_CANONICAL_HOST
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR
LT_INIT

AC_DEFINE([_GNU_SOURCE], [1], [Enable GNU extensions])

# Require libarchive
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive])

# Require OpenSSL (for libfetch).  1.1.1 is the floor: it gives us
# TLS_client_method() and X509_check_host(), so src/libfetch/ carries no
# compatibility shims for either.
PKG_CHECK_MODULES([OPENSSL], [openssl >= 1.1.1])

# Require libsolv
AC_CHECK_HEADER([solv/pool.h], [],
  [AC_MSG_ERROR([libsolv headers not found])])
AC_CHECK_LIB([solv], [pool_create], [:],
  [AC_MSG_ERROR([libsolv library not found])])
AC_CHECK_LIB([solvext], [repo_add_debpackages], [:],
  [AC_MSG_ERROR([libsolvext library not found])])

# clang-format, for the format targets only.  Not required to build.
#
# The major is recorded so the format targets can refuse one that is too
# old.  19 is a floor, not a pin: nothing in .clang-format needs that exact
# version, and a newer one that formatted this tree differently would fail
# format-check with a concrete diff rather than drift quietly.  Older ones
# are refused because they predate settings the file uses.
#
# The unversioned binary is preferred, since that is what the build
# dependency names and it tracks the distribution's default; the versioned
# one is a fallback for a machine that has it without the metapackage.
AC_CHECK_PROGS([CLANG_FORMAT], [clang-format clang-format-19], [no])
AC_SUBST([CLANG_FORMAT])

CLANG_FORMAT_MAJOR=unknown
if test "$CLANG_FORMAT" != no; then
    AC_MSG_CHECKING([clang-format major version])
    CLANG_FORMAT_MAJOR=`$CLANG_FORMAT --version 2>/dev/null | sed -e 's/.*version //' -e 's/\..*//'`
    if test -z "$CLANG_FORMAT_MAJOR"; then
        CLANG_FORMAT_MAJOR=unknown
    fi
    AC_MSG_RESULT([$CLANG_FORMAT_MAJOR])
fi
AC_SUBST([CLANG_FORMAT_MAJOR])

# python3 runs the test servers (condserver.py and friends) and the
# coverage aggregator.  Not required to build, and the tests that need it
# skip themselves without it.
AC_CHECK_PROGS([PYTHON], [python3], [no])
AC_SUBST([PYTHON])

# gcov instrumentation, for "make coverage".  Off by default: it is a
# measurement build, not a shipping one.
#
# -O0 is not optional.  At -O2 gcc merges and clones basic blocks and the
# line attribution stops meaning anything, so a coverage figure from an
# optimised build measures something other than the source.
#
# The flags go on per-target CFLAGS rather than onto CFLAGS itself, so a
# coverage build does not quietly discard the -O2 the user asked for --
# it is visible in the Makefiles which targets are instrumented.
AC_ARG_ENABLE([coverage],
  [AS_HELP_STRING([--enable-coverage],
    [build with gcov instrumentation for "make coverage" @<:@default=no@:>@])],
  [], [enable_coverage=no])

if test "$enable_coverage" = yes; then
    COVERAGE_CFLAGS="-O0 -g --coverage"
    COVERAGE_LDFLAGS="--coverage"
    # ... but per-target CFLAGS alone are not enough to make -O0 stick.
    # Automake emits "$(target_CFLAGS) $(CFLAGS)", so CFLAGS lands LAST on
    # every command line, and gcc takes the last -O it is given.  AC_PROG_CC
    # defaults CFLAGS to "-g -O2" when it is unset, and dpkg-buildpackage
    # exports an -O2 of its own, so the -O0 above was being overridden in
    # both the documented developer workflow and the package build: every
    # compile line read "-O0 -g --coverage -g -O2" and the coverage figures
    # were measured on an OPTIMISED build.  That is the thing the paragraph
    # above says must not happen, and it moved the numbers by 2.3 points
    # overall -- it also moves the denominators, because merged and cloned
    # basic blocks change which lines are countable at all.
    #
    # Only the optimisation level is removed, and only where it stands as
    # its own word, so everything else the user asked for still reaches the
    # compiler.  Replacing CFLAGS wholesale would discard their -march, -D
    # and warning flags along with it.
    CFLAGS=`echo "$CFLAGS" | sed -e 's/\(^\| \)-O[[0-9a-z]]*/\1/g'`
    if test "$PYTHON" = no; then
        AC_MSG_ERROR([--enable-coverage needs python3 for the aggregator])
    fi
fi
AC_SUBST([COVERAGE_CFLAGS])
AC_SUBST([COVERAGE_LDFLAGS])
AM_CONDITIONAL([COVERAGE], [test "$enable_coverage" = yes])

# Threads, for the concurrency test only.  On glibc >= 2.34 and musl
# the pthread symbols live in libc, so this usually resolves to nothing.
save_LIBS=$LIBS
LIBS=
AC_SEARCH_LIBS([pthread_create], [pthread], [], [])
PTHREAD_LIBS=$LIBS
LIBS=$save_LIBS
AC_SUBST([PTHREAD_LIBS])

# Optional static linking for libarchive and/or libsolv
AC_ARG_ENABLE([static-libarchive],
  [AS_HELP_STRING([--enable-static-libarchive],
    [statically link libarchive @<:@default=no@:>@])],
  [], [enable_static_libarchive=no])

AC_ARG_ENABLE([static-libsolv],
  [AS_HELP_STRING([--enable-static-libsolv],
    [statically link libsolv and libsolvext @<:@default=no@:>@])],
  [], [enable_static_libsolv=no])

if test "$enable_static_libarchive" = yes; then
    _deps=`$PKG_CONFIG --libs --static libarchive 2>/dev/null | sed 's/-larchive//g'`
    ARCHIVE_LIBS="-Wl,-Bstatic -larchive -Wl,-Bdynamic $_deps"
else
    ARCHIVE_LIBS="$LIBARCHIVE_LIBS"
fi
AC_SUBST([ARCHIVE_LIBS])

if test "$enable_static_libsolv" = yes; then
    _deps=`$PKG_CONFIG --libs --static libsolv libsolvext 2>/dev/null | sed 's/-lsolvext//g; s/-lsolv//g'`
    SOLV_LIBS="-Wl,-Bstatic -lsolvext -lsolv -Wl,-Bdynamic $_deps"
else
    SOLV_LIBS="-lsolvext -lsolv"
fi
AC_SUBST([SOLV_LIBS])

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
