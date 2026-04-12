LIBFETCH_PATCH = $(top_srcdir)/patches/libfetch-fixes.patch
LIBFETCH_STAMP = $(top_srcdir)/libfetch/.patched

$(LIBFETCH_STAMP): $(LIBFETCH_PATCH)
	cd $(top_srcdir) && patch -p1 --forward < patches/libfetch-fixes.patch
	@touch $@

BUILT_SOURCES = $(LIBFETCH_STAMP)

clean-local:
	@if [ -f $(LIBFETCH_STAMP) ]; then \
	    cd $(top_srcdir) && patch -p1 -R --dry-run < patches/libfetch-fixes.patch >/dev/null 2>&1 \
	        && patch -p1 -R < patches/libfetch-fixes.patch; \
	    rm -f $(LIBFETCH_STAMP); \
	fi

lib_LTLIBRARIES = libaept.la
bin_PROGRAMS = aept

libaept_la_SOURCES = \
    api.c \
    autoremove.c \
    clash.c \
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
