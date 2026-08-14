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
    include/aept/index.h \
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

EXTRA_DIST = .clang-format .git-blame-ignore-revs

# ── formatting ───────────────────────────────────────────────────────────
#
# Every commit is expected to be clang-format clean; see CLAUDE.md.  "format"
# is the one to use day to day -- it touches only what you have changed, so it
# never buries your diff under an unrelated reformat.  "format-all" exists for
# a config change or a fresh import, and "format-check" is the read-only form
# for a hook or CI.

FORMAT_DIRS = src include tests

# Everything this working tree has touched: unstaged, staged and new.  Paths
# that were deleted are dropped, since they cannot be formatted.
FORMAT_CHANGED = \
	{ git diff --name-only; \
	  git diff --cached --name-only; \
	  git ls-files --others --exclude-standard; } 2>/dev/null \
	| sort -u | grep -E '\.[ch]$$' \
	| while read f; do test -f "$$f" && echo "$$f"; done

FORMAT_ALL = find $(FORMAT_DIRS) \( -name '*.c' -o -name '*.h' \) -print | sort

# Fail loudly rather than silently doing nothing.
check_clang_format = \
	if test "$(CLANG_FORMAT)" = no; then \
	    echo "clang-format not found; re-run configure after installing it" >&2; \
	    exit 1; \
	fi

format:
	@$(check_clang_format); \
	cd $(top_srcdir) || exit 1; \
	if ! git rev-parse --git-dir >/dev/null 2>&1; then \
	    echo "make format needs a git checkout; use make format-all" >&2; \
	    exit 1; \
	fi; \
	files=`$(FORMAT_CHANGED)`; \
	if test -z "$$files"; then \
	    echo "no changed C files to format"; \
	else \
	    $(CLANG_FORMAT) -i $$files || exit 1; \
	    echo "formatted:"; echo "$$files" | sed 's/^/  /'; \
	fi

format-all:
	@$(check_clang_format); \
	cd $(top_srcdir) || exit 1; \
	files=`$(FORMAT_ALL)`; \
	$(CLANG_FORMAT) -i $$files || exit 1; \
	echo "formatted `echo $$files | wc -w` files"

format-check:
	@$(check_clang_format); \
	cd $(top_srcdir) || exit 1; \
	files=`$(FORMAT_ALL)`; \
	$(CLANG_FORMAT) --dry-run -Werror $$files

readme:
	$(srcdir)/scripts/update-readme.sh

.PHONY: readme format format-all format-check
