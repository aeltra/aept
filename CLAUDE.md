# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
./configure          # generate Makefiles (run once, or after configure.ac changes)
make                 # build src/aept binary
make clean           # remove build artifacts
```

Build dependencies: libarchive (pkg-config), libsolv + libsolvext (AC_CHECK_LIB). No test suite exists.

## Project Overview

**aept** (Aeltra Package Tool) is a minimal package manager for .ipk packages with dependency resolution. It handles update/install/remove/upgrade operations using libsolv for dependency solving and libarchive for archive extraction. External tools: wget (downloads), usign (signature verification).

## Coding Conventions

- **4 spaces** indentation, no tabs
- Header guards: `{NAME}_H_7BF97F` suffix (e.g. `ARCHIVE_H_7BF97F`)
- Project headers: `#include "aept/foo.h"` — all in `include/aept/`
- Autotools config: `#include <config.h>`
- Functions return 0 on success, -1 on error
- Error cleanup via `goto cleanup` pattern
- OOM-safe allocators: `xmalloc()`, `xrealloc()`, `xstrdup()`, `xasprintf()`
- Types use `_t` suffix: `aept_config_t`, `aept_source_t`
- `_GNU_SOURCE` defined in configure.ac (needed for `unshare`, `CLONE_NEWUSER`)
- License: GPL-2.0-or-later; file headers include SPDX and copyright

## Architecture

**Global config singleton** (`cfg` in config.c) — `aept_config_t` holds all paths, sources, flags, and architectures. Initialized from `/etc/aept/aept.conf` by `config_load()`, freed by `config_free()`.

**Command flow** (main.c): parse CLI opts → `config_load()` → dispatch to `aept_update()`, `aept_install()`, `aept_remove()`, or `aept_upgrade()` → `config_free()`.

**Key subsystems:**

- **solver.c** — Wraps libsolv pool/repo/solver/transaction. Loads Packages files via `repo_add_debpackages()` (from `<solv/repo_deb.h>`). Retrieves download filenames via `solvable_lookup_location()`. Max 64 repos.
- **archive.c** — Two-level IPK extraction (outer AR → inner tar). Adapted from opkg's `opkg_archive.c`. Handles nested decompression with libarchive callbacks. Compression support (gzip always; xz/bzip2/lz4/zstd compile-time via `HAVE_*`).
- **install.c** — Orchestrates: load repos → solve → download → extract control → preinst → extract data → record file list → postinst → update status.
- **remove.c** — Orchestrates: solve removal → prerm → delete files from .list → postrm → clean info dir → update status.
- **status.c** — Reads/writes the installed-packages database (Debian control format). Loaded into libsolv as the "@installed" repo.
- **util.c** — `xsystem()` / `xsystem_offline_root()` for subprocess execution. Offline root uses `unshare(CLONE_NEWUSER)` + uid/gid mapping + chroot for non-root installs.
- **script.c** — Runs maintainer scripts (preinst/postinst/prerm/postrm) with `PKG_ROOT` env var set.
