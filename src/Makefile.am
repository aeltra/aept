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
# current:revision:age.  aept_set_network_timeout() and aept_last_error()
# were added and nothing was removed or changed, so this is a
# backwards-compatible growth: current and age both up, revision back to 0.
# The soname is current - age, so it stays libaept.so.0 and the binary
# package stays libaept0 -- old callers keep working without a rebuild.
libaept_la_LDFLAGS = -version-info 1:0:1

aept_SOURCES = main.c
aept_CFLAGS = $(LIBARCHIVE_CFLAGS) -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = libaept.la
