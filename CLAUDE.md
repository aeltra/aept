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
make format          # clang-format the files you changed (see Coding Conventions)
```

Build dependencies: libarchive and OpenSSL **>= 1.1.1** (pkg-config), libsolv + libsolvext
(AC_CHECK_LIB). `src/libfetch/` is a **fork**, no longer tracked upstream — edit it
directly; there is no patch series and no re-import script. It has been pruned
to what aept uses: HTTP and HTTPS GET, the connection cache, redirects, proxies
and basic auth from the source URL. Uploads, stat, directory listing, `.netrc`,
`HTTP_AUTH` and `HTTP_PROXY_AUTH` are gone. **Credentials come from a URL and
nowhere else** — the source URL for an origin server, the `$HTTP_PROXY` URL for
a proxy; there is no environment variable that supplies a user and password.
Range requests, restart/resume, `If-Modified-Since` and `struct url_stat` are
gone as well: aept never set `url->offset`, so that machinery was unreachable
by design yet live enough that an **unsolicited `206 Partial Content` was
accepted and written out as a whole file**. `206` and `416` are now protocol
errors. `tests/test_http.sh` characterises its behaviour — run it after any
change there. It carries **no OpenSSL compatibility shims**: 1.1.1 is the floor, so
`TLS_client_method()` and `X509_check_host()` are used unguarded, and
`src/libfetch/common.h` includes `<openssl/{err,ssl,x509,x509v3}.h>` directly.
Its identifiers are all `libfetch_*` / `LIBFETCH_*` now, snake_case like the
rest of the tree, and its two OpenSSL setup helpers return **0 on success,
-1 on error** like everything else — they used to return 1 on success, which
is the inverse. aept includes it as `"libfetch/fetch.h"`, not `<fetch.h>`:
it is a vendored header, not a system one.

**The tree builds warning-free.** `make check CFLAGS="-O2 -g -Wall -Wextra
-Wno-unused-parameter"` emits nothing, tests included, so any warning at all is
one you introduced. Note that a `CFLAGS` change does not force a recompile —
`make clean` first or you are reading stale objects.

## Project Overview

**aept** (Aeltra Package Tool) is a minimal package manager for .aeltra packages with dependency resolution. It handles update/install/remove/upgrade operations using libsolv for dependency solving and libarchive for archive extraction. Uses libfetch for downloads. External tools: usign (signature verification), plus `rm`, `diff` and `/bin/sh`. Every one of them is exec'd by absolute path from the `AEPT_*_BIN` defines in `internal.h` — aept normally runs as root, so no exec may resolve through `PATH` or `$SHELL`.

**Symbol visibility.** `libaept` is built with `-fvisibility=hidden`, so the ABI
is what `AEPT_API` marks in the headers — not whatever is spelled `aept_*`. It
exports **34** symbols: the 31 in `aept.h`, plus `aept_log()`,
`aept_malloc()` and `aept_asprintf()`, which the CLI needs because it links
`libaept` like any other consumer. Before this it exported 139, including every
internal helper, and `src/libfetch/` stayed hidden only because its names
failed the old `-export-symbols-regex '^aept_'`. Tests that reach past `aept.h`
link the static archive (`_LDFLAGS = -static` in `tests/Makefile.am`), where
hidden visibility does not apply. Check with
`nm -D --defined-only src/.libs/libaept.so`, **after `make clean`** — a
`CFLAGS` change in `Makefile.am` does not force a recompile, so a stale tree
reports the old surface.

## Coding Conventions

**All code is formatted with clang-format before it is committed.** No
exceptions and no hand-tuning afterwards — the config is the style, and a
commit that leaves the tree unformatted is a commit that will be reformatted by
somebody else's edit later.

```bash
make format        # format what you changed: unstaged, staged and new files
make format-all    # the whole tree — for a .clang-format change or a fresh import
make format-check  # read-only; silent when clean, names offenders when not
```

`make format` is the one for day-to-day use: it touches only the files you
have changed, so it never buries your diff under an unrelated reformat.
`make format-check` exits non-zero on the first badly formatted file, so it
works as a pre-commit hook or a CI step.

`.clang-format` at the top level encodes the style and governs the **whole
tree**, `src/libfetch/` included. It is written against **clang-format 19** —
check `clang-format --version` before a tree-wide run, because other versions
differ slightly and will churn lines they should not.

Two settings are chosen for stability under editing rather than looks, because
a formatter that reflows lines you did not touch breaks string-matching edits:
`ColumnLimit: 100` (renaming a widely-used identifier reflowed 77 unrelated
lines at 80 columns, 19 at 100) and `ReflowComments: false`. Comment prose is
wrapped by hand. `BreakStringLiterals: false` keeps a log or usage message on
one line so it can still be grepped for.

The tree-wide reformat is listed in `.git-blame-ignore-revs`; enable it with
`git config blame.ignoreRevsFile .git-blame-ignore-revs`.

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
- Shell tests source `tests/aeptlib.sh` (`make_pkg`, `make_pkg_conffile`,
  `new_root`, `aept_run`, `make_keypair`, `make_inpackages`, `http_serve`) and
  `skip` (exit 77) when a tool is missing rather than failing.
- libfetch is built without `file.c`, so HTTP/HTTPS are the only transports.
  Tests needing a real fetch serve over loopback via `http_serve`.
- Register new tests in `check_PROGRAMS` or `dist_check_SCRIPTS` in
  `tests/Makefile.am`; new headers go in `noinst_HEADERS` in the top-level
  `Makefile.am`, or `make distcheck` breaks.
- `httpget`, `httpstub.py` and `threadrace` are harnesses driven by shell
  tests, not tests themselves: they are in `check_PROGRAMS` but deliberately
  absent from `unit_tests`, so `TESTS` never runs them directly.

**Data races.** `make check` catches crashes and wrong answers, but a race that
happens to come out right passes silently — this was demonstrated: with the
`api_sort_pool` bug restored, `test_threads.sh` still reported zero failures
while ThreadSanitizer flagged it every run. So check races with TSan, not the
suite:

```bash
gcc -fsanitize=thread -g -O1 -D_GNU_SOURCE -I. -Iinclude -Isrc/libfetch \
    -o /tmp/threadrace_tsan tests/threadrace.c \
    $(ls src/*.c | grep -v main.c) src/libfetch/*.c \
    $(pkg-config --cflags --libs libarchive openssl) -lsolvext -lsolv -lpthread
setarch $(uname -m) -R /tmp/threadrace_tsan 20 <root-a> <root-b> <url>
```

`setarch -R` is required: TSan aborts with "unexpected memory mapping" under
the ASLR settings on current kernels. Build the roots the way
`test_threads.sh` does. Do not add a TSan build to `make check` — it needs its
own build of everything.

## Architecture

**Opaque context handle** — `aept_ctx_t` (opaque in `aept.h`, defined in `internal.h`) owns all state: config, solver, lock fd, callbacks, cancellation flag. Created by `aept_init()`, destroyed by `aept_cleanup(ctx)`. All public API functions take `ctx` as the first argument. Different threads may operate on independent contexts concurrently (e.g. different offline roots); `aept_cancel()` is safe to call from any thread.

Downloading used to be the exception, because libfetch kept its state in process globals. That is no longer so: the connection cache, the client certificate and the network timeout live in the per-context `struct libfetch_ctx` (`ctx->http`, created by `aept_init()`), and the error state is `_Thread_local`. **Nothing at file scope in `src/libfetch/` is mutable any more.** The last two globals, `libfetch_timeout` and `libfetch_restart_calls`, are gone: the first became `ctx->timeout`, stamped onto each connection and refreshed from the context when one comes back out of the cache; the second was a knob nobody set, guarding branches that never ran.

**Getting out of a transfer that is going nowhere.** Every wait in libfetch — connect, the TLS handshake, reads, writes — goes through `libfetch_wait()`, and every one of them is a `poll()`. That is not incidental. `poll(2)` is never restarted after a signal whatever flags the handler carries, whereas `read(2)` *is* restarted under `SA_RESTART`, which glibc's `signal(3)` sets by default and which `signal.siginterrupt(sig, False)` sets in Python. Waiting anywhere else costs an embedder the ability to abandon a stalled transfer, and it costs the timeout its coverage. This is why `libfetch_ssl()` puts the socket into non-blocking mode for the life of a TLS connection and drives `SSL_connect()`/`SSL_write()` through `WANT_READ`/`WANT_WRITE` loops: with a blocking socket OpenSSL waits inside its own `read(2)`, where neither property holds. EINTR is reported, never retried, so the caller can act on it.

Two escapes, in order of importance: a caller *must not* have to send a signal to get control back, because a signal acts on the whole process — hence `option network_timeout` (default 120s, `0` disables), an **idle** timeout re-armed on every wait, so a slow transfer completes and only a silent one is cut off. It reports `AEPT_ERR_TIMEOUT` via `aept_last_error()`, which the Python bindings turn into `AeptTimeout`. And a signal *must still* work for the CLI's Ctrl-C. `tests/test_timeout.sh` and `tests/test_cancel.sh` hold both down, the latter covering `SA_RESTART` explicitly. One wait is beyond reach: `getaddrinfo(3)` retries internally on EINTR, so an unreachable nameserver blocks for as long as `resolv.conf` allows — about ten seconds by default — neither bounded nor interruptible.

**TLS verification is not configurable, by design.** Peer verification is `SSL_VERIFY_PEER` and the `X509_check_host()` hostname check is unconditional; upstream's `SSL_NO_VERIFY_HOSTNAME` escape hatch is gone. The client certificate comes only from `aept_config` via `libfetch_set_client_certificate()` — upstream's `SSL_CLIENT_{CERT,KEY}_FILE` fallback is gone too, so a stray environment variable cannot make aept authenticate as somebody else. The trust store is loaded by naming `X509_get_default_cert_file()` and `X509_get_default_cert_dir()` explicitly rather than by calling `SSL_CTX_set_default_verify_paths()`, **because the latter resolves the store through `$SSL_CERT_FILE`/`$SSL_CERT_DIR`** — one variable replaces every trusted CA with the attacker's own, and it returns success either way. Verified: with `SSL_CERT_FILE` pointed at a self-signed CA, `set_default_verify_paths()` yields a store containing exactly that CA and nothing else.

`tests/test_threads.sh` drives five contexts at once — two listing, one downloading, two cycling a package through install and remove — so the solver, archive extraction, the status database, the owner index and triggers all run concurrently. The same harness is clean under ThreadSanitizer (below).

All three `fork()` sites are async-signal-safe in the child: `aept_system()` and `aept_verify_signature()` do nothing but `exec` + `_exit`, and `aept_system_offline_root()` was reworked to match — pre-formatted uid/gid maps, literal `/proc/self/` paths, and `child_err()` instead of logging. No `setenv()` remains anywhere in aept, so libfetch's `getenv()` reads are races only if the *embedding application* mutates the environment concurrently. `readdir()` is used on per-context `DIR *` streams, and `dirname()`/`basename()` on private copies.

Not verified: OpenSSL's self-initialisation, and libsolv/libarchive safety for independent objects. Both are relied upon; neither is tested here.

A consequence worth knowing: the cache limits (4 connections, 2 per host) are now *per context*, not per process, so N contexts can hold up to 4N idle sockets.

**Logging** uses a thread-local pointer (`_Thread_local` in msg.c) set by `aept_init()`. Log macros (`aept_log_error`, etc.) take no context parameter — they read from the thread-local pointer. Display/confirm callbacks and `aept_cancelled()` also read from it.

**Context parameter conventions:** public API and orchestrators take `aept_ctx_t *ctx`; pure config functions take `struct aept_config *cfg`; pure solver accessors take `aept_solver_t *s`; pure utilities take no context.

**Command flow** (main.c): parse CLI opts → `aept_init()` → `aept_load_config(ctx, path)` → dispatch to `aept_update(ctx)`, `aept_install(ctx, ...)`, etc. → `aept_cleanup(ctx)`.

**Key subsystems:**

- **solver.c** — Wraps libsolv pool/repo/solver/transaction. Loads Packages files via `repo_add_debpackages()` (from `<solv/repo_deb.h>`). Retrieves download filenames via `solvable_lookup_location()`. Max 64 repos.
- **archive.c** — Two-level extraction (outer AR → inner tar), the `.deb`/`.ipk` container layout. Handles nested decompression with libarchive callbacks. Originally adapted from opkg and GPL-licensed; **rewritten from scratch and relicensed MIT in `4f0989d`** — do not reintroduce opkg code here. Compression support (gzip always; xz/bzip2/lz4/zstd compile-time via `HAVE_*`).
- **install.c** — Orchestrates: load repos → solve → download → extract control → preinst → extract data → record file list → postinst → update status.
- **remove.c** — Orchestrates: solve removal → prerm → delete files from .list → postrm → clean info dir → update status.
- **status.c** — Reads/writes the installed-packages database (Debian control format). Loaded into libsolv as the "@installed" repo.
- **update.c** — Fetches package lists. With `check_signature` on (the default) it fetches **`InPackages.gz`** — index and signature in one object — and there is deliberately **no** fallback to `Packages` + `Packages.sig`, so blocking one request cannot push a client onto the two-object path. Decompression is capped at `MAX_INDEX_SIZE`, since the stream is chosen by the remote side.
- **clearsign.c** — Splits a signify clearsigned envelope into message and signature. Splits at the **last** signature marker, because the envelope has no escaping and a package `Description` can contain a line that looks like one. The signature covers the index bytes exactly, trailing newline included.
- **verify.c** — Invokes usign via the absolute `AEPT_USIGN_BIN`. usign has no clearsign verify mode, only detached `-m message -x sigfile`, which is why clearsign.c splits first. `usign -V -P <dir>` looks the key up by the fingerprint embedded in the signature, expecting `<dir>/<fingerprint>`. Note `aept_config_apply_offline_root()` does not prefix `usign_keydir`, by design — verification always uses the host trust store.
- **conffile.c** — Conffile hashes in `{info_dir}/{name}.conffiles`. On upgrade, `aept_conffile_resolve_upgrade()` rewrites the file from the *new* set and runs *before* install.c's `remove_info_files()` — which is why that function's extension list deliberately omits `conffiles`.
- **owner_index.c / clash.c** — In-memory path → owning-package index, built once per transaction and threaded through install/upgrade/remove so later clash checks see earlier steps.
- **trigger.c** — Directory-watch triggers from `{info_dir}/{name}.triggers`, matched via `fnmatch` against directories touched by the transaction.
- **download.c** — wraps `src/libfetch/` for HTTP/HTTPS retrieval of indexes and packages. The only caller of the fork outside `api.c`, which sets up and tears down its connection cache. A body that ends before its `Content-Length`, or a chunked body that ends mid-chunk or without its CRLF framing, is an **error**, not a short read: `struct httpio` sets `error`, so the stream fails and its connection is dropped rather than returned to the cache. Only the checksum saves a truncated package; nothing saves a truncated unsigned index.
- **api.c** — Public API implementation behind `aept.h`; **pin.c** version pinning, **autoremove.c** unneeded auto-installed packages, **clean.c** cache cleanup.
- **util.c** — `aept_system()` / `aept_system_offline_root()` for subprocess execution. Offline root uses `unshare(CLONE_NEWUSER)` + uid/gid mapping + chroot for non-root installs. Also the `aept_fgets_is_truncated()` / `aept_fgets_drain_line()` pair every line reader in the tree uses to drop over-long lines rather than parse them in pieces.
- **script.c** — Runs maintainer scripts (preinst/postinst/prerm/postrm) via `/bin/sh` through `aept_system_offline_root()`. No environment is set for them: with an offline root the script runs chrooted, so it already sees that root as `/` and needs no prefix variable (unlike opkg, which does not chroot and passes `$PKG_ROOT` instead).
