# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
autoreconf -i        # after any configure.ac / Makefile.am change
./configure          # generate Makefiles (run once, or after the above)
make                 # build src/aept and libaept
make check           # run the test suite
make check VERBOSE=1 # ... dumping failing output to the console
make distcheck       # VPATH build from the release tarball + suite
make clean           # remove build artifacts
```

Build dependencies: libarchive and OpenSSL (pkg-config), libsolv + libsolvext
(AC_CHECK_LIB). `libfetch/` is a **fork**, no longer tracked upstream — edit it
directly; there is no patch series and no re-import script. It has been pruned
to what aept uses: HTTP and HTTPS GET, the connection cache, redirects, proxies
and basic auth from the source URL. Uploads, stat, directory listing, `.netrc`
and `HTTP_AUTH` are gone. `tests/test_http.sh` characterises its behaviour —
run it after any change there.

Warning baseline for `make CFLAGS="-O2 -g -Wall -Wextra -Wno-unused-parameter"`:
three `-Wcomment` in `include/aept/` (`status.h`, `trigger.h`, `owner_index.h`)
and two `-Wsign-compare` in vendored libfetch. Anything else is new.

## Project Overview

**aept** (Aeltra Package Tool) is a minimal package manager for .aep packages with dependency resolution. It handles update/install/remove/upgrade operations using libsolv for dependency solving and libarchive for archive extraction. Uses libfetch for downloads. External tools: usign (signature verification), plus `rm`, `diff` and `/bin/sh`. Every one of them is exec'd by absolute path from the `AEPT_*_BIN` defines in `internal.h` — aept normally runs as root, so no exec may resolve through `PATH` or `$SHELL`.

## Coding Conventions

- **4 spaces** indentation, no tabs
- Header guards: `{NAME}_H_7BF97F` suffix (e.g. `ARCHIVE_H_7BF97F`)
- Project headers: `#include "aept/foo.h"` — all in `include/aept/`
- Autotools config: `#include <config.h>`
- Functions return 0 on success, -1 on error
- Error cleanup via `goto cleanup` pattern
- OOM-safe allocators: `aept_malloc()`, `aept_realloc()`, `aept_strdup()`, `aept_asprintf()`
- Types use `_t` suffix: `aept_config_t`, `aept_source_t`
- `_GNU_SOURCE` defined in configure.ac (needed for `unshare`, `CLONE_NEWUSER`)
- License: MIT; file headers include SPDX and copyright

## Testing

`tests/` holds TAP-style unit tests in C and integration tests in shell, both
run by Automake's harness.

- **Every fix ships with a test that has been shown to fail without the fix.**
  Revert the fix, run `make check`, confirm red, restore. A test that has never
  failed proves nothing.
- Unit tests that need file-scope helpers `#include` the `.c` directly — see
  `test_archive_path.c` (`archive.c`) and `test_trigger_entries.c`
  (`trigger.c`); otherwise link the module normally.
- Tests that log expected errors install a quiet context — see
  `silence_logging()` in `test_clearsign.c` — so a passing run stays clean.
- Shell tests source `tests/aeptlib.sh` (`make_aep`, `make_aep_conffile`,
  `new_root`, `aept_run`, `make_keypair`, `make_inpackages`, `http_serve`) and
  `skip` (exit 77) when a tool is missing rather than failing.
- libfetch is built without `file.c`, so HTTP/HTTPS are the only transports.
  Tests needing a real fetch serve over loopback via `http_serve`.
- Register new tests in `check_PROGRAMS` or `dist_check_SCRIPTS` in
  `tests/Makefile.am`; new headers go in `noinst_HEADERS` in the top-level
  `Makefile.am`, or `make distcheck` breaks.

## Architecture

**Opaque context handle** — `aept_ctx_t` (opaque in `aept.h`, defined in `internal.h`) owns all state: config, solver, lock fd, callbacks, cancellation flag. Created by `aept_init()`, destroyed by `aept_cleanup(ctx)`. All public API functions take `ctx` as the first argument. Different threads may operate on independent contexts concurrently (e.g. different offline roots), **except across operations that download** — `aept_update()`, `aept_install()`. libfetch keeps all of its state in process globals: the connection cache (`common.c:304`) is a list every fetch mutates, the error state (`fetch.c:44`) is one struct, and the client certificate selected by `fetch_set_client_certificate()` belongs to whichever thread set it last. Callers must serialise download-bearing calls until that changes. `aept_cancel()` is safe to call from any thread.

**Logging** uses a thread-local pointer (`_Thread_local` in msg.c) set by `aept_init()`. Log macros (`aept_log_error`, etc.) take no context parameter — they read from the thread-local pointer. Display/confirm callbacks and `aept_cancelled()` also read from it.

**Context parameter conventions:** public API and orchestrators take `aept_ctx_t *ctx`; pure config functions take `struct aept_config *cfg`; pure solver accessors take `aept_solver_t *s`; pure utilities take no context.

**Command flow** (main.c): parse CLI opts → `aept_init()` → `aept_load_config(ctx, path)` → dispatch to `aept_update(ctx)`, `aept_install(ctx, ...)`, etc. → `aept_cleanup(ctx)`.

**Key subsystems:**

- **solver.c** — Wraps libsolv pool/repo/solver/transaction. Loads Packages files via `repo_add_debpackages()` (from `<solv/repo_deb.h>`). Retrieves download filenames via `solvable_lookup_location()`. Max 64 repos.
- **archive.c** — Two-level AEP extraction (outer AR → inner tar). Adapted from opkg's `opkg_archive.c`. Handles nested decompression with libarchive callbacks. Compression support (gzip always; xz/bzip2/lz4/zstd compile-time via `HAVE_*`).
- **install.c** — Orchestrates: load repos → solve → download → extract control → preinst → extract data → record file list → postinst → update status.
- **remove.c** — Orchestrates: solve removal → prerm → delete files from .list → postrm → clean info dir → update status.
- **status.c** — Reads/writes the installed-packages database (Debian control format). Loaded into libsolv as the "@installed" repo.
- **update.c** — Fetches package lists. With `check_signature` on (the default) it fetches **`InPackages.gz`** — index and signature in one object — and there is deliberately **no** fallback to `Packages` + `Packages.sig`, so blocking one request cannot push a client onto the two-object path. Decompression is capped at `MAX_INDEX_SIZE`, since the stream is chosen by the remote side.
- **clearsign.c** — Splits a signify clearsigned envelope into message and signature. Splits at the **last** signature marker, because the envelope has no escaping and a package `Description` can contain a line that looks like one. The signature covers the index bytes exactly, trailing newline included.
- **verify.c** — Invokes usign via the absolute `AEPT_USIGN_BIN`. usign has no clearsign verify mode, only detached `-m message -x sigfile`, which is why clearsign.c splits first. `usign -V -P <dir>` looks the key up by the fingerprint embedded in the signature, expecting `<dir>/<fingerprint>`. Note `aept_config_apply_offline_root()` does not prefix `usign_keydir`, by design — verification always uses the host trust store.
- **conffile.c** — Conffile hashes in `{info_dir}/{name}.conffiles`. On upgrade, `aept_conffile_resolve_upgrade()` rewrites the file from the *new* set and runs *before* install.c's `remove_info_files()` — which is why that function's extension list deliberately omits `conffiles`.
- **owner_index.c / clash.c** — In-memory path → owning-package index, built once per transaction and threaded through install/upgrade/remove so later clash checks see earlier steps.
- **trigger.c** — Directory-watch triggers from `{info_dir}/{name}.triggers`, matched via `fnmatch` against directories touched by the transaction.
- **download.c** — libfetch wrapper for HTTP/HTTPS retrieval of indexes and packages.
- **api.c** — Public API implementation behind `aept.h`; **pin.c** version pinning, **autoremove.c** unneeded auto-installed packages, **clean.c** cache cleanup.
- **util.c** — `aept_system()` / `aept_system_offline_root()` for subprocess execution. Offline root uses `unshare(CLONE_NEWUSER)` + uid/gid mapping + chroot for non-root installs. Also the `aept_fgets_is_truncated()` / `aept_fgets_drain_line()` pair every line reader in the tree uses to drop over-long lines rather than parse them in pieces.
- **script.c** — Runs maintainer scripts (preinst/postinst/prerm/postrm) via `/bin/sh` through `aept_system_offline_root()`. No environment is set for them: with an offline root the script runs chrooted, so it already sees that root as `/` and needs no prefix variable (unlike opkg, which does not chroot and passes `$PKG_ROOT` instead).
