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
    install.c \
    owner_index.c \
    remove.c \
    update.c \
    status.c \
    trigger.c \
    util.c \
    msg.c \
    pin.c \
    libfetch/common.c \
    libfetch/fetch.c \
    libfetch/http.c \
    libfetch/common.h \
    libfetch/fetch.h

# -fvisibility=hidden: the ABI is what AEPT_API marks in the headers, not
# whatever is spelled aept_*.  This is also what keeps src/libfetch/ out of
# the dynamic symbol table now that its own prefix no longer does the job.
libaept_la_CFLAGS = -D_GNU_SOURCE -fvisibility=hidden \
    $(LIBARCHIVE_CFLAGS) $(OPENSSL_CFLAGS) \
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
# new library rather than dying on a missing symbol.  0:1:0 is where
# aept_set_network_timeout() and aept_last_error() put us: added, nothing
# removed, so callers built against libaept.so.0 keep working unrebuilt.
libaept_la_LDFLAGS = -version-number 0:1:0

aept_SOURCES = main.c
aept_CFLAGS = $(LIBARCHIVE_CFLAGS) -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = libaept.la
