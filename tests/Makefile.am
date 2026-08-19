AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/src \
    -I$(srcdir)

# Unit tests run as tests; httpget, partialget and stallclient are
# harnesses the shell tests drive, so they are built but never run on
# their own.
unit_tests = test_archive_path test_script test_config_root \
    test_archive_open test_clearsign test_trigger_entries test_ssl_env \
    test_no_proxy test_validator test_url_sanitize test_auto_flags

check_PROGRAMS = $(unit_tests) httpget partialget threadrace stallclient

noinst_HEADERS = test.h

httpget_SOURCES = httpget.c
httpget_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
# -static: libaept is built with -fvisibility=hidden, so internals such
# as aept_download() are absent from the shared object's symbol table.
# The static archive keeps them, and these three reach past aept.h.
httpget_LDADD = $(top_builddir)/src/libaept.la
httpget_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# Another harness for test_http.sh, this one going to libfetch directly:
# what it does -- stop reading a response part-way -- is precisely what
# aept_download() never does.
partialget_SOURCES = partialget.c
partialget_CFLAGS = -D_GNU_SOURCE $(OPENSSL_CFLAGS) $(COVERAGE_CFLAGS)
partialget_LDADD = $(top_builddir)/src/libaept.la
partialget_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# A harness for test_cancel.sh and test_timeout.sh.  Links the library
# rather than individual modules, for the same reason httpget does:
# aept_download() is not part of the ABI.
stallclient_SOURCES = stallclient.c
stallclient_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
stallclient_LDADD = $(top_builddir)/src/libaept.la
stallclient_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# Also a harness, driven by test_threads.sh.
threadrace_SOURCES = threadrace.c
threadrace_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
threadrace_LDADD = $(top_builddir)/src/libaept.la $(PTHREAD_LIBS)
threadrace_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# archive.c is #included by the test, so only its dependencies are listed
# here; util.c and msg.c supply the allocators and logging it calls into.
test_archive_path_SOURCES = \
    test_archive_path.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_archive_path_CFLAGS = -D_GNU_SOURCE $(LIBARCHIVE_CFLAGS) $(COVERAGE_CFLAGS)
test_archive_path_LDFLAGS = $(COVERAGE_LDFLAGS)
test_archive_path_LDADD = $(ARCHIVE_LIBS)

test_script_SOURCES = \
    test_script.c \
    $(top_srcdir)/src/script.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_script_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_script_LDFLAGS = $(COVERAGE_LDFLAGS)

test_config_root_SOURCES = \
    test_config_root.c \
    $(top_srcdir)/src/config.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_config_root_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_config_root_LDFLAGS = $(COVERAGE_LDFLAGS)

test_archive_open_SOURCES = \
    test_archive_open.c \
    $(top_srcdir)/src/archive.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_archive_open_CFLAGS = -D_GNU_SOURCE $(LIBARCHIVE_CFLAGS) $(COVERAGE_CFLAGS)
test_archive_open_LDFLAGS = $(COVERAGE_LDFLAGS)
test_archive_open_LDADD = $(ARCHIVE_LIBS)

test_clearsign_SOURCES = \
    test_clearsign.c \
    $(top_srcdir)/src/clearsign.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_clearsign_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_clearsign_LDFLAGS = $(COVERAGE_LDFLAGS)

# trigger.c is #included by the test, so only its dependencies are
# listed here.
test_trigger_entries_SOURCES = \
    test_trigger_entries.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_trigger_entries_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_trigger_entries_LDFLAGS = $(COVERAGE_LDFLAGS)

# validator.c reads and writes the record beside an index; nothing in it
# reaches the network, so only the allocators and logging come with it.
test_validator_SOURCES = \
    test_validator.c \
    $(top_srcdir)/src/validator.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_validator_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_validator_LDFLAGS = $(COVERAGE_LDFLAGS)

test_url_sanitize_SOURCES = \
    test_url_sanitize.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/libfetch/pctdecode.c \
    $(top_srcdir)/src/msg.c
test_url_sanitize_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_url_sanitize_LDFLAGS = $(COVERAGE_LDFLAGS)

# Links the library rather than individual modules: status.c's database
# half pulls in the solver and through it libsolv.
test_auto_flags_SOURCES = test_auto_flags.c
test_auto_flags_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_auto_flags_LDADD = $(top_builddir)/src/libaept.la
test_auto_flags_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# Links the library rather than individual modules: aept_download()
# pulls in libfetch and its OpenSSL dependency.
test_ssl_env_SOURCES = test_ssl_env.c
test_ssl_env_CFLAGS = -D_GNU_SOURCE $(COVERAGE_CFLAGS)
test_ssl_env_LDADD = $(top_builddir)/src/libaept.la
test_ssl_env_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# Links the library for the same reason test_ssl_env does, and needs the
# OpenSSL headers because libfetch/common.h includes them directly.
test_no_proxy_SOURCES = test_no_proxy.c
test_no_proxy_CFLAGS = -D_GNU_SOURCE $(OPENSSL_CFLAGS) $(COVERAGE_CFLAGS)
test_no_proxy_LDADD = $(top_builddir)/src/libaept.la
test_no_proxy_LDFLAGS = -static $(COVERAGE_LDFLAGS)

# Integration tests drive the built binary against a throwaway offline
# root; they skip themselves (exit 77) when aept or ar/tar are missing.
dist_check_SCRIPTS = \
    test_install_failure.sh \
    test_install_from_repo.sh \
    test_package_checksum.sh \
    test_file_clash.sh \
    test_remove.sh \
    test_upgrade.sh \
    test_install_rollback.sh \
    test_triggers.sh \
    test_autoremove.sh \
    test_clean.sh \
    test_mark.sh \
    test_pin.sh \
    test_offline_root_tmp.sh \
    test_install_local_noop.sh \
    test_update_inpackages.sh \
    test_signature_reject.sh \
    test_update_conditional.sh \
    test_redact_credentials.sh \
    test_index_freshness.sh \
    test_upgrade_conffiles.sh \
    test_hardlink_escape.sh \
    test_update_size_limit.sh \
    test_abi_symbols.sh \
    test_exec_path.sh \
    test_ssl_client_cert.sh \
    test_http.sh \
    test_threads.sh \
    test_cancel.sh \
    test_timeout.sh \
    test_signal_resume.sh

EXTRA_DIST = aeptlib.sh ptydrive.py httpstub.py stallserver.py condserver.py \
    dribbleserver.py libaept.abi abi-declarations.sh \
    coverage-report.py coverage.tiers coverage.baseline

TEST_EXTENSIONS = .sh

# Run shell tests through $(SHELL) so they work even if the executable
# bit is lost in a checkout or a release tarball.
SH_LOG_COMPILER = $(SHELL)

AM_TESTS_ENVIRONMENT = \
    AEPT_BIN='$(abs_top_builddir)/src/aept'; export AEPT_BIN; \
    srcdir='$(abs_srcdir)'; export srcdir; \
    HTTPGET='$(abs_builddir)/httpget'; export HTTPGET; \
    PARTIALGET='$(abs_builddir)/partialget'; export PARTIALGET; \
    THREADRACE='$(abs_builddir)/threadrace'; export THREADRACE; \
    STALLCLIENT='$(abs_builddir)/stallclient'; export STALLCLIENT; \
    LIBAEPT_SO='$(abs_top_builddir)/src/.libs/libaept.so'; export LIBAEPT_SO; \
    NM='$(NM)'; export NM; \
    ABI_INCLUDEDIR='$(abs_top_srcdir)/include'; export ABI_INCLUDEDIR; \
    CC='$(CC)'; export CC;

# A coverage build is not ABI-representative: libgcov's runtime symbols
# (__gcov_dump, __gcov_master, __gcov_var, ...) and mangle_path come out
# of libgcov.a, which is not compiled with -fvisibility=hidden, so they
# land in the dynamic symbol table.  test_abi_symbols.sh is right to
# object and skips itself on this rather than being weakened to tolerate
# it -- the ABI is checked by every ordinary build.
if COVERAGE
AM_TESTS_ENVIRONMENT += AEPT_COVERAGE=1; export AEPT_COVERAGE;
endif

TESTS = $(unit_tests) $(dist_check_SCRIPTS)
