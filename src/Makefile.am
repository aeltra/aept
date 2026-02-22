lib_LTLIBRARIES = libaept.la
bin_PROGRAMS = aept

libaept_la_SOURCES = \
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
    query.c \
    remove.c \
    update.c \
    status.c \
    util.c \
    msg.c \
    pin.c

libaept_la_CFLAGS = $(LIBARCHIVE_CFLAGS) -I$(top_builddir) -I$(top_srcdir)/include
libaept_la_LIBADD = $(SOLV_LIBS) $(ARCHIVE_LIBS)
libaept_la_LDFLAGS = -version-info 0:0:0

aept_SOURCES = main.c
aept_CFLAGS = $(LIBARCHIVE_CFLAGS) -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = libaept.la
