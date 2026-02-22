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

# Require libsolv
AC_CHECK_HEADER([solv/pool.h], [],
  [AC_MSG_ERROR([libsolv headers not found])])
AC_CHECK_LIB([solv], [pool_create], [:],
  [AC_MSG_ERROR([libsolv library not found])])
AC_CHECK_LIB([solvext], [repo_add_debpackages], [:],
  [AC_MSG_ERROR([libsolvext library not found])])

# Probe compression support in libarchive
AC_CHECK_LIB([archive], [archive_read_support_filter_xz],
  [AC_DEFINE([HAVE_XZ], [1], [libarchive has xz support])])
AC_CHECK_LIB([archive], [archive_read_support_filter_bzip2],
  [AC_DEFINE([HAVE_BZIP2], [1], [libarchive has bzip2 support])])
AC_CHECK_LIB([archive], [archive_read_support_filter_lz4],
  [AC_DEFINE([HAVE_LZ4], [1], [libarchive has lz4 support])])
AC_CHECK_LIB([archive], [archive_read_support_filter_zstd],
  [AC_DEFINE([HAVE_ZSTD], [1], [libarchive has zstd support])])

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

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
