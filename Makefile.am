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
    include/aept/validator.h \
    include/aept/verify.h

# Files that are not built but must ship, or the release tarball cannot
# produce what the git tree produces.
#
# Watch what does NOT catch a mistake here: "make distcheck" runs
# configure, make and make check against the unpacked tarball, and none
# of those touch the manpage source, the bindings or NOTICE.  distcheck
# passing is evidence that the tarball builds, not that it is complete.
# All three of these were missing while distcheck was green.
#
#   NOTICE          the BSD attributions for libarchive and the other
#                   third-party code.  Their terms require a source
#                   redistribution to carry them, and the tarball is one.
#   aept.1.scd      debian/rules runs scdoc over it to build the manpage,
#                   so without it a package build from the tarball fails.
#   python/         debian/rules builds a wheel from it -- same story.
#                   Listed file by file rather than as a directory, so a
#                   stray __pycache__ or build/ cannot ride along.
#   docs/, scripts/ so "make docs" works from an unpacked tarball.
EXTRA_DIST = .clang-format .git-blame-ignore-revs \
    NOTICE \
    aept.1.scd \
    docs/aept.1.md \
    scripts/render-manual.sh \
    python/pyproject.toml \
    python/aeltra/aept/__init__.py \
    python/aeltra/aept/_cdef.py \
    python/aeltra/aept/_ffi.py \
    python/aeltra/aept/_marshalling.py \
    python/aeltra/aept/aept.py \
    python/examples/demo.py

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

# Render the manpage into docs/aept.1.md, which the README links to.
# The README itself is written by hand: it is the page somebody lands on,
# and that is a different job from the reference.
docs:
	$(srcdir)/scripts/render-manual.sh

# ── coverage ─────────────────────────────────────────────────────────────
#
# Needs ./configure --enable-coverage, and its own build directory: a
# CFLAGS change does not force a recompile, so a tree that was built
# without instrumentation stays uninstrumented and the report comes out
# empty (or worse, stale).
#
#   make coverage         report, whatever the suite did
#   make coverage-check   the gate: per-file floors and no tier regressing
#   make coverage-update  re-bless tests/coverage.baseline
#
# "coverage" reports and "coverage-check" judges, deliberately kept apart.
# Reporting a number the gate would reject is the honest thing for the
# report to do; a single target that refused to print would leave you
# without the figure exactly when you need it.
#
# The suite is run serially.  Parallel processes merging into the same
# .gcda is a race, and a race in the measurement is worse than a slow
# measurement.
COVERAGE_REPORT = $(PYTHON) $(top_srcdir)/tests/coverage-report.py \
	--build-dir $(top_builddir) --srcdir $(top_srcdir)

check_coverage_enabled = \
	if test -z '$(COVERAGE_CFLAGS)'; then \
	    echo "not a coverage build; reconfigure with --enable-coverage" >&2; \
	    echo "  (in a separate build directory -- a CFLAGS change does not" >&2; \
	    echo "   force a recompile, so an existing tree stays stale)" >&2; \
	    exit 1; \
	fi

# All three clear the counters first, so the figure depends on the tree
# and not on how many times you have run the suite.  gcov counters
# accumulate across runs by design -- a second run reports the union of
# both -- which is right for building up a picture by hand and wrong for
# a number that is committed and compared.  Measured: running the suite
# twice moved the transaction tier by a line and five branches, all from
# tests whose covered path varies with timing.  Small, and small drift
# that nobody can reproduce is the worst kind.
#
# To accumulate deliberately, run "make check" as often as you like and
# then invoke tests/coverage-report.py directly.
#
# The suite failing does not invalidate the numbers, so this reports
# either way -- but it says so, because a red suite is a red suite.
coverage:
	@$(check_coverage_enabled)
	@$(MAKE) $(AM_MAKEFLAGS) coverage-clean
	-@$(MAKE) $(AM_MAKEFLAGS) -j1 check
	@if test -n "`find $(top_builddir) -name '*.gcda' -print -quit`"; then :; else \
	    echo "no profile data was produced" >&2; exit 1; fi
	@$(COVERAGE_REPORT) --uncovered

coverage-check:
	@$(check_coverage_enabled)
	@$(MAKE) $(AM_MAKEFLAGS) coverage-clean
	-@$(MAKE) $(AM_MAKEFLAGS) -j1 check
	@$(COVERAGE_REPORT) --check

coverage-update:
	@$(check_coverage_enabled)
	@$(MAKE) $(AM_MAKEFLAGS) coverage-clean
	-@$(MAKE) $(AM_MAKEFLAGS) -j1 check
	@$(COVERAGE_REPORT) --update

coverage-clean:
	@find $(top_builddir) -name '*.gcda' -delete

# Re-bless the ABI baseline that tests/test_abi_symbols.sh checks against.
# Run it after deliberately changing an interface, together with the
# -version-number bump in src/Makefile.am that the change requires: a
# removal or a signature change needs major++, an addition minor++.
# The comment header is kept; only the declarations below it are replaced.
abi-update:
	@abi=$(top_srcdir)/tests/libaept.abi; \
	sed -n '1,/^$$/p' $$abi > $$abi.new; \
	CC='$(CC)' $(SHELL) $(top_srcdir)/tests/abi-declarations.sh \
	    $(top_srcdir)/include >> $$abi.new || { rm -f $$abi.new; exit 1; }; \
	mv $$abi.new $$abi; \
	echo "blessed `grep -c '^[a-z]' $$abi` interfaces in tests/libaept.abi"

.PHONY: docs format format-all format-check abi-update \
	coverage coverage-check coverage-update coverage-clean
