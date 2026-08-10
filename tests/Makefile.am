AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/src \
    -I$(srcdir)

# Unit tests run as tests; httpget is a harness test_http.sh drives, so
# it is built but never run on its own.
unit_tests = test_archive_path test_script test_config_root \
    test_archive_open test_clearsign test_trigger_entries test_ssl_env

check_PROGRAMS = $(unit_tests) httpget threadrace

noinst_HEADERS = test.h

httpget_SOURCES = httpget.c
httpget_CFLAGS = -D_GNU_SOURCE
# -static: libaept is built with -fvisibility=hidden, so internals such
# as aept_download() are absent from the shared object's symbol table.
# The static archive keeps them, and these three reach past aept.h.
httpget_LDADD = $(top_builddir)/src/libaept.la
httpget_LDFLAGS = -static

# Also a harness, driven by test_threads.sh.
threadrace_SOURCES = threadrace.c
threadrace_CFLAGS = -D_GNU_SOURCE
threadrace_LDADD = $(top_builddir)/src/libaept.la $(PTHREAD_LIBS)
threadrace_LDFLAGS = -static

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

# trigger.c is #included by the test, so only its dependencies are
# listed here.
test_trigger_entries_SOURCES = \
    test_trigger_entries.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_trigger_entries_CFLAGS = -D_GNU_SOURCE

# Links the library rather than individual modules: aept_download()
# pulls in libfetch and its OpenSSL dependency.
test_ssl_env_SOURCES = test_ssl_env.c
test_ssl_env_CFLAGS = -D_GNU_SOURCE
test_ssl_env_LDADD = $(top_builddir)/src/libaept.la
test_ssl_env_LDFLAGS = -static

# Integration tests drive the built binary against a throwaway offline
# root; they skip themselves (exit 77) when aept or ar/tar are missing.
dist_check_SCRIPTS = \
    test_install_failure.sh \
    test_offline_root_tmp.sh \
    test_install_local_noop.sh \
    test_update_inpackages.sh \
    test_upgrade_conffiles.sh \
    test_update_size_limit.sh \
    test_exec_path.sh \
    test_ssl_client_cert.sh \
    test_http.sh \
    test_threads.sh

EXTRA_DIST = aeptlib.sh ptydrive.py httpstub.py

TEST_EXTENSIONS = .sh

# Run shell tests through $(SHELL) so they work even if the executable
# bit is lost in a checkout or a release tarball.
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    AEPT_BIN='$(abs_top_builddir)/src/aept'; export AEPT_BIN; \
    srcdir='$(abs_srcdir)'; export srcdir; \
    HTTPGET='$(abs_builddir)/httpget'; export HTTPGET; \
    THREADRACE='$(abs_builddir)/threadrace'; export THREADRACE;

TESTS = $(unit_tests) $(dist_check_SCRIPTS)
