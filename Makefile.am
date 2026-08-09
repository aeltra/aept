ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src tests

# aept.h is the public API and is the only header that gets installed.
aeptincludedir = $(includedir)/aept
aeptinclude_HEADERS = include/aept/aept.h

# The remaining headers are internal, but they must still ship in the
# tarball or a release cannot be built from it.
noinst_HEADERS = \
    include/aept/archive.h \
    include/aept/autoremove.h \
    include/aept/clash.h \
    include/aept/clean.h \
    include/aept/clearsign.h \
    include/aept/conffile.h \
    include/aept/config.h \
    include/aept/download.h \
    include/aept/install.h \
    include/aept/internal.h \
    include/aept/msg.h \
    include/aept/owner_index.h \
    include/aept/pin.h \
    include/aept/remove.h \
    include/aept/script.h \
    include/aept/solver.h \
    include/aept/status.h \
    include/aept/trigger.h \
    include/aept/update.h \
    include/aept/util.h \
    include/aept/verify.h

readme:
	$(srcdir)/scripts/update-readme.sh

.PHONY: readme
