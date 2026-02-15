bin_PROGRAMS = aept

aept_SOURCES = \
    main.c \
    autoremove.c \
    clean.c \
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
    msg.c

aept_CFLAGS = $(LIBARCHIVE_CFLAGS) -I$(top_builddir) -I$(top_srcdir)/include
aept_LDADD = $(SOLV_LIBS) $(ARCHIVE_LIBS)
