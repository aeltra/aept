AM_CPPFLAGS = \
    -I$(top_builddir) \
    -I$(top_srcdir)/include \
    -I$(top_srcdir)/src \
    -I$(srcdir)

check_PROGRAMS = test_archive_path

noinst_HEADERS = test.h

# archive.c is #included by the test, so only its dependencies are listed
# here; util.c and msg.c supply the allocators and logging it calls into.
test_archive_path_SOURCES = \
    test_archive_path.c \
    $(top_srcdir)/src/util.c \
    $(top_srcdir)/src/msg.c
test_archive_path_CFLAGS = -D_GNU_SOURCE $(LIBARCHIVE_CFLAGS)
test_archive_path_LDADD = $(ARCHIVE_LIBS)

TESTS = $(check_PROGRAMS)
