AC_INIT([aept], [0.1.0])
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
AC_CHECK_PROGS([CLANG_FORMAT], [clang-format], [no])
AC_SUBST([CLANG_FORMAT])

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
