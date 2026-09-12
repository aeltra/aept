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
# One entry per released soname, not per change: what a consumer sees is
# the version they can link against, and several bumps between two
# releases would only record the order the work happened in.
#
# 1:0:0  Two changed interfaces, so the major moves and the binary
#        package becomes libaept1:
#
#          * aept_set_offline_root() and aept_set_cache_dir() return int
#            instead of void.  They are the only setters that allocate,
#            and aept_last_error() alone could not carry the failure --
#            it is documented for a call that returned non-zero, which a
#            void function never does, and aept_download() resets it.
#          * aept_pkg_info_t gained section, source, maintainer and
#            download_size.  The caller allocates that struct and passes
#            it to aept_show(), so a library writing the wider one into
#            an older caller's stack is why this renames rather than
#            adds -- 128 bytes became 160.
#
#        Added in the same bump: AEPT_ERR_NOMEM, aept_show_all(),
#        aept_pkg_info_list_free() and aept_pkg_info_list_t.
#
#        Note for the next enumerator: test_abi_symbols.sh reports an
#        appended one as *changed*, because the baseline records
#        declarations as text.  It is an addition -- the earlier
#        enumerators keep their values and the enum's size is unchanged
#        -- so minor++ is the answer.  One inserted or renumbered would
#        be a real break.
# 0:3:0  aept_set_cache_dir()
# 0:2:0  aept_triggers(), AEPT_ERR_TRIGGER
# 0:1:0  aept_set_network_timeout(), aept_last_error()
libaept_la_LDFLAGS = -version-number 1:0:0 $(COVERAGE_LDFLAGS)

aept_SOURCES = main.c
aept_CFLAGS = $(LIBARCHIVE_CFLAGS) $(COVERAGE_CFLAGS) \
    -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = libaept.la
aept_LDFLAGS = $(COVERAGE_LDFLAGS)
