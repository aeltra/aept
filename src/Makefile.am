lib_LTLIBRARIES = libaept.la
bin_PROGRAMS = aept

libaept_la_SOURCES = \
    api.c \
    autoremove.c \
    clash.c \
    clearsign.c \
    clean.c \
    conffile.c \
    config.c \
    download.c \
    verify.c \
    solver.c \
    archive.c \
    script.c \
    index.c \
    install.c \
    owner_index.c \
    remove.c \
    update.c \
    stanza.c \
    status.c \
    trigger.c \
    util.c \
    msg.c \
    pin.c \
    validator.c \
    libfetch/common.c \
    libfetch/fetch.c \
    libfetch/http.c \
    libfetch/pctdecode.c \
    libfetch/common.h \
    libfetch/fetch.h

# -fvisibility=hidden: the ABI is what AEPT_API marks in the headers, not
# whatever is spelled aept_*.  This is also what keeps src/libfetch/ out of
# the dynamic symbol table now that its own prefix no longer does the job.
libaept_la_CFLAGS = -D_GNU_SOURCE -fvisibility=hidden \
    $(LIBARCHIVE_CFLAGS) $(OPENSSL_CFLAGS) $(COVERAGE_CFLAGS) \
    -I$(top_builddir) -I$(top_srcdir)/include
libaept_la_LIBADD = $(SOLV_LIBS) $(ARCHIVE_LIBS) $(OPENSSL_LIBS)
# -version-number major:minor:revision, which reads in the same order as
# the file it produces: libaept.so.<major>.<minor>.<revision>, with the
# soname libaept.so.<major> and hence the binary package libaept<major>.
#
# Deliberately not -version-info, whose current:revision:age describes a
# range of supported interfaces and has to be converted in your head --
# 1:0:1 for the same result, with the soname coming out as current - age.
# Same thing to libtool (see build-aux/ltmain.sh), unreadable to people.
# The trade is portability we do not use: the two spellings agree on
# linux, darwin and the BSDs, and disagree on sunos, sco and irix.
#
# To bump:
#   implementation changed, interfaces did not   revision++
#   interfaces added, none removed or changed    minor++,  revision = 0
#   any interface removed or changed             major++,  minor = revision = 0
#
# That last line renames the soname, so old binaries refuse to load the
# new library rather than dying on a missing symbol.
#
# 1:1:0  aept_show_all(), aept_pkg_info_list_free() and the
#        aept_pkg_info_list_t they use.  Added, nothing removed.
#        aept_pkg_info_t.is_installed now describes the version in hand
#        rather than the package: the struct is unchanged, so this is a
#        behaviour fix rather than an interface change.
# 1:0:0  aept_set_offline_root() and aept_set_cache_dir() return int
#        instead of void.  A changed interface, so the binary package
#        becomes libaept1.
# 0:4:0  AEPT_ERR_NOMEM.  test_abi_symbols.sh reports the enum as
#        changed, because the baseline records declarations as text, but
#        this is an addition: the earlier enumerators keep 0..3 and the
#        enum's size is unchanged.  An enumerator inserted or renumbered
#        would be a real break.
# 0:3:0  aept_set_cache_dir()
# 0:2:0  aept_triggers(), AEPT_ERR_TRIGGER
# 0:1:0  aept_set_network_timeout(), aept_last_error()
libaept_la_LDFLAGS = -version-number 1:1:0 $(COVERAGE_LDFLAGS)

aept_SOURCES = main.c
aept_CFLAGS = $(LIBARCHIVE_CFLAGS) $(COVERAGE_CFLAGS) \
    -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = libaept.la
aept_LDFLAGS = $(COVERAGE_LDFLAGS)
