AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/src \
    -I$(srcdir)

check_PROGRAMS = test_archive_path test_script test_config_root \
    test_archive_open test_clearsign

noinst_HEADERS = test.h

# archive.c is #included by the test, so only its dependencies are listed
# here; util.c and msg.c supply the allocators and logging it calls into.
test_archive_path_SOURCES = \
    test_archive_path.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_archive_path_CFLAGS = -D_GNU_SOURCE $(LIBARCHIVE_CFLAGS)
test_archive_path_LDADD = $(ARCHIVE_LIBS)

test_script_SOURCES = \
    test_script.c \
    $(top_srcdir)/src/script.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_script_CFLAGS = -D_GNU_SOURCE

test_config_root_SOURCES = \
    test_config_root.c \
    $(top_srcdir)/src/config.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_config_root_CFLAGS = -D_GNU_SOURCE

test_archive_open_SOURCES = \
    test_archive_open.c \
    $(top_srcdir)/src/archive.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_archive_open_CFLAGS = -D_GNU_SOURCE $(LIBARCHIVE_CFLAGS)
test_archive_open_LDADD = $(ARCHIVE_LIBS)

test_clearsign_SOURCES = \
    test_clearsign.c \
    $(top_srcdir)/src/clearsign.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_clearsign_CFLAGS = -D_GNU_SOURCE

# Integration tests drive the built binary against a throwaway offline
# root; they skip themselves (exit 77) when aept or ar/tar are missing.
dist_check_SCRIPTS = \
    test_install_failure.sh \
    test_offline_root_tmp.sh \
    test_install_local_noop.sh \
    test_update_inpackages.sh \
    test_upgrade_conffiles.sh

EXTRA_DIST = aeptlib.sh

TEST_EXTENSIONS = .sh

# Run shell tests through $(SHELL) so they work even if the executable
# bit is lost in a checkout or a release tarball.
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    AEPT_BIN='$(abs_top_builddir)/src/aept'; export AEPT_BIN; \
    srcdir='$(abs_srcdir)'; export srcdir;

TESTS = $(check_PROGRAMS) $(dist_check_SCRIPTS)
