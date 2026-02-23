ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src

aeptincludedir = $(includedir)/aept
aeptinclude_HEADERS = include/aept/aept.h

readme:
	$(srcdir)/scripts/update-readme.sh

update-libfetch:
	$(srcdir)/scripts/update-libfetch.sh

.PHONY: readme update-libfetch
