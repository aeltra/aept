AC_INIT([aept], [0.1.0])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])

AC_CANONICAL_HOST
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR

AC_DEFINE([_GNU_SOURCE], [1], [Enable GNU extensions])

# Require libarchive
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive])

# Require libsolv
AC_CHECK_HEADER([solv/pool.h], [],
  [AC_MSG_ERROR([libsolv headers not found])])
AC_CHECK_LIB([solv], [pool_create], [],
  [AC_MSG_ERROR([libsolv library not found])])
AC_CHECK_LIB([solvext], [repo_add_debpackages], [],
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

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
