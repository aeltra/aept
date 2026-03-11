lib_LTLIBRARIES = libaept.la
bin_PROGRAMS = aept

libaept_la_SOURCES = \
    api.c \
    autoremove.c \
    clean.c \
    conffile.c \
    config.c \
    download.c \
    verify.c \
    solver.c \
    archive.c \
    script.c \
    install.c \
    remove.c \
    update.c \
    status.c \
    trigger.c \
    util.c \
    msg.c \
    pin.c \
    ../libfetch/common.c \
    ../libfetch/fetch.c \
    ../libfetch/http.c \
    ../libfetch/openssl-compat.c

libaept_la_CFLAGS = -D_GNU_SOURCE $(LIBARCHIVE_CFLAGS) $(OPENSSL_CFLAGS) \
    -I$(top_builddir) -I$(top_srcdir)/include -I$(top_srcdir)/libfetch
libaept_la_LIBADD = $(SOLV_LIBS) $(ARCHIVE_LIBS) $(OPENSSL_LIBS)
libaept_la_LDFLAGS = -version-info 0:0:0

aept_SOURCES = main.c
aept_CFLAGS = $(LIBARCHIVE_CFLAGS) -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = libaept.la
