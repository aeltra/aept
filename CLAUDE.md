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
Range requests, restart/resume and `struct url_stat` are gone as well: aept
never set `url->offset`, so that machinery was unreachable by design yet live
enough that an **unsolicited `206 Partial Content` was accepted and written out
as a whole file**. `206` and `416` are now protocol errors, and so is an
unsolicited `304` — see the conditional-GET note under update.c.
`tests/test_http.sh` characterises its behaviour — run it after any
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
hidden visibility does not apply.

**The ABI baseline.** `tests/libaept.abi` records the ABI as *declarations*,
not as names — 45 interfaces: the 34 `AEPT_API` functions, plus every type and
enum in `aept.h`. `tests/test_abi_symbols.sh` checks it, and
`tests/abi-declarations.sh` extracts the current one by preprocessing the
headers with `-DAEPT_API=AEPT_EXPORT` (everything from `aept.h`, since it is
the public header; only marked declarations from the internal ones). Whole
declarations, because a symbol table has no signatures in it: a name list
cannot see that `aept_install()` grew an argument, that a field was added to
`aept_pkg_info_t`, or that the `AEPT_FLAG_*` enumerators were reordered — and
those break un-recompiled callers *silently*, which is worse than a symbol
going missing. Regenerate with **`make abi-update`**.

The three cases are not treated alike, following the `-version-number` rules
in `src/Makefile.am`:

- **removed** or **changed** → **fails**. Needs `major++, minor = revision = 0`,
  so old binaries are refused at load time rather than dying at run time.
- **added** → **passes**, with a note. An addition breaks no existing caller.
  It needs `minor++, revision = 0` before release and an `abi-update` so it is
  guarded from then on.

Two further checks hold regardless of the baseline: the exported set must equal
exactly what `AEPT_API` marks (so neither a leaked internal nor a declared-but-
missing function passes — this is what the old `-export-symbols-regex '^aept_'`
could not do, since `src/libfetch/` stayed hidden only by failing the prefix),
and every symbol must be a function (`T`), since `AEPT_API` marks nothing else.
An exported signature naming a type the baseline does not record also fails,
or that type's layout would be ABI with nothing watching it.

`nm` needs no configure check: libtool cannot build a shared library without
it, so `$(NM)` — substituted by `LT_INIT`, host-prefixed on a cross build — is
already guaranteed, and `binutils` comes with `build-essential` on Debian. Run
**after `make clean`** if you have changed `CFLAGS` or `LDFLAGS`: a
`Makefile.am` flag change does not force a recompile, so a stale tree reports
the old surface. `abidiff` would add little here — every public type is either
opaque (`struct aept_ctx`) or defined in `aept.h` out of primitives, so the
declared text *is* the ABI.

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
  `new_root`, `aept_run`, `make_keypair`, `make_inpackages`, `http_serve`,
  `cond_serve`, `dribble_serve`) and `skip` (exit 77) when a tool is missing
  rather than failing.
- libfetch is built without `file.c`, so HTTP/HTTPS are the only transports.
  Tests needing a real fetch serve over loopback via `http_serve`. `cond_serve`
  starts `condserver.py` instead when the test is about revalidation: it emits
  `ETag` and `Last-Modified`, honours both conditional headers with the RFC's
  precedence, and **logs what each request asked**. python3's own `http.server`
  handles `If-Modified-Since` but emits no `ETag` and reports nothing, so a test
  built on it cannot tell a conditional request answered `304` from one that was
  never made — which is exactly the distinction being tested. `dribble_serve`
  starts `dribbleserver.py`, which sends a body one piece at a time — and in
  chunked mode splits each chunk header from its own CRLF, and each chunk from
  its trailer — so the client spends the transfer waiting in `poll()` and a
  signal has somewhere to land. A server answering at loopback speed opens no
  such window, and the test would pass without ever exercising anything.
- Register new tests in `check_PROGRAMS` or `dist_check_SCRIPTS` in
  `tests/Makefile.am`; new headers go in `noinst_HEADERS` in the top-level
  `Makefile.am`, or `make distcheck` breaks.
- `httpget`, `partialget`, `httpstub.py`, `stallclient` and `threadrace` are
  harnesses driven by shell tests, not tests themselves: they are in
  `check_PROGRAMS` but deliberately absent from `unit_tests`, so `TESTS` never
  runs them directly. `partialget` is the one that goes to libfetch directly
  rather than through `aept_download()`, because what it does — stop reading a
  response part-way — is precisely what `aept_download()` never does.
- **`make` does not build `check_PROGRAMS`.** Running `./tests/test_foo` or a
  shell test after a plain `make` runs the *previous* binary, so a revert probe
  can come out green on a fix that is no longer there. This has now happened
  twice. Build the harness first: `make -C tests stallclient`, or `make check`.

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

**Getting out of a transfer that is going nowhere.** Every wait in libfetch — connect, the TLS handshake, reads, writes — goes through `libfetch_wait()`, and every one of them is a `poll()`. That is not incidental. `poll(2)` is never restarted after a signal whatever flags the handler carries, whereas `read(2)` *is* restarted under `SA_RESTART`, which glibc's `signal(3)` sets by default and which `signal.siginterrupt(sig, False)` sets in Python. Waiting anywhere else costs an embedder the ability to abandon a stalled transfer, and it costs the timeout its coverage. This is why `libfetch_ssl()` puts the socket into non-blocking mode for the life of a TLS connection and drives `SSL_connect()`/`SSL_write()` through `WANT_READ`/`WANT_WRITE` loops: with a blocking socket OpenSSL waits inside its own `read(2)`, where neither property holds. It is also why `libfetch_read()` keeps EINTR out of the `SSL_ERROR_SYSCALL` flattening to `EIO`: a signal must arrive at the caller as a signal. (With a non-blocking socket the wait is in `poll()` and OpenSSL should never see EINTR itself, so that guard is belt and braces — it is not covered by a test.)

**A signal is reported, never retried down there — and never treated as a stream failure either.** Those are two different rules and both matter. libfetch reports EINTR rather than looping on it so the decision belongs to the caller, which is what lets `download.c` check `aept_cancelled()` *before* it retries and what lets a signal end a handshake that would otherwise hang: before a body has begun there is no reader to decide, so an interruption there is final. But an interrupted wait consumed nothing, so the stream survives it, and `struct httpio` keeps a transient `intr` apart from the sticky `error` that condemns a connection. Latching the two together is what made `download.c`'s `if (errno == EINTR) continue;` **unable to succeed** — the retry it cost landed on a stream that had already given up, was restated as `EIO`, and the transfer died of somebody else's `SIGWINCH`. Resuming is state, not luck: a chunk's closing CRLF is read at the top of the *next* fill (`trailer`/`trailer_got`), and `libfetch_getln()` resumes a half-read line instead of restarting it (`conn->line_partial`) — restarting would re-read the tail of a line as if it were the next one, and in a chunked body the lines *are* the framing. A connection therefore goes back in the cache only when its stream **ended at the end of its response** (`io->end_of_body`), not merely when it did not fail: anything else leaves bytes of this response on the wire for the next request to be answered by. `tests/test_signal_resume.sh` drives both framings under a signal storm; `/abandon` in `tests/httpstub.py` covers the caching rule with leftovers built to *parse*, since binary ones only produce a protocol error and a protocol error on a cached connection is silently retried on a fresh one.

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
- **update.c** — Fetches package lists. With `check_signature` on (the default) it fetches **`InPackages.gz`** — index and signature in one object — and there is deliberately **no** fallback to `Packages` + `Packages.sig`, so blocking one request cannot push a client onto the two-object path. Decompression is capped at `MAX_INDEX_SIZE`, since the stream is chosen by the remote side. **The signature is checked here and nowhere else** — `aept_verify_signature()` has exactly one caller — so the stored index is trusted thereafter because it was verified once, when written. The `.sig` written beside it is never read again.

  **Conditional GET.** The index request carries `If-None-Match` and `If-Modified-Since` when both are known, per RFC 9110 §13.1.3, and a `304` means keep what is on disk — no download, no re-verification, since it was verified when it was written. Packages are never revalidated: they are immutable and already checksummed. The validators live in `<lists_dir>/<name>.validator` (`validator.c`), the **first** per-source state file in the tree, as **opaque tokens echoed verbatim** — an ETag and an HTTP-date are equally unparsed, so there is no date parser anywhere in aept. That is the whole reason for a side file: apt keeps the validator in the cached file's mtime and is therefore forced through `time_t`, needing a parser *and* a generator, in a store any `cp` without `-a` resets. The record names the URL it was written for and is only sent back to that URL, or the same source fetched as `Packages.gz` instead of `InPackages.gz` would revalidate against the wrong document. It is written **last**, after the index has been verified and stored — a validator recorded for an index that was then rejected would earn a `304` and freeze the client on a document it never accepted. An **unsolicited `304` is a protocol error** in libfetch, on the same grounds as an unsolicited `206`: a request that asked no question gets no answer. **None of this is a freshness mechanism** — `ETag`, `Last-Modified` and `304` are all unsigned, a mirror answering "not modified" forever *is* the freeze attack, and only index.c's signed `Valid-Until` bounds it. `tests/test_update_conditional.sh` and `tests/test_validator.c` cover it.
- **index.c** — the repository metadata stanza an index opens with (`Origin`, `Date`, `Valid-Until`) instead of a package. Timestamps are UTC and fixed width, so comparing two is `strcmp` and needs no date parsing. `aept_index_check_expiry()` runs at **index load time** (`install.c` and `api.c`), not at update time, and that placement is the point: the client this catches is one whose updates never arrive, and an attacker who simply drops the request leaves a client on a stale index forever where nothing on the update path can see it. `option check_index_expiry` decides whether expiry is fatal — default `0` (warn and use it anyway), because enforcing needs a re-signing job republishing on a timer, and without one every archive that stops receiving uploads expires and every client stops working. A deployment that runs such a job sets it to `1`; `install` then fails and `api.c` skips the source rather than aborting the whole query. An index carrying no `Valid-Until` is never refused, or repositories indexed before the field existed would break. **Rollback is not defended against**: `Date` is emitted and signed, but nothing compares it to the index already held. `tests/test_index_freshness.sh` covers the rest.
- **clearsign.c** — Splits a signify clearsigned envelope into message and signature. Splits at the **last** signature marker, because the envelope has no escaping and a package `Description` can contain a line that looks like one. The signature covers the index bytes exactly, trailing newline included.
- **verify.c** — Invokes usign via the absolute `AEPT_USIGN_BIN`. usign has no clearsign verify mode, only detached `-m message -x sigfile`, which is why clearsign.c splits first. `usign -V -P <dir>` looks the key up by the fingerprint embedded in the signature, expecting `<dir>/<fingerprint>`. Note `aept_config_apply_offline_root()` does not prefix `usign_keydir`, by design — verification always uses the host trust store.
- **conffile.c** — Conffile hashes in `{info_dir}/{name}.conffiles`. On upgrade, `aept_conffile_resolve_upgrade()` rewrites the file from the *new* set and runs *before* install.c's `remove_info_files()` — which is why that function's extension list deliberately omits `conffiles`.
- **owner_index.c / clash.c** — In-memory path → owning-package index, built once per transaction and threaded through install/upgrade/remove so later clash checks see earlier steps.
- **trigger.c** — Directory-watch triggers from `{info_dir}/{name}.triggers`, matched via `fnmatch` against directories touched by the transaction.
- **download.c** — wraps `src/libfetch/` for HTTP/HTTPS retrieval of indexes and packages. The only caller of the fork outside `api.c`, which sets up and tears down its connection cache. A body that ends before its `Content-Length`, or a chunked body that ends mid-chunk or without its CRLF framing, is an **error**, not a short read: `struct httpio` sets `error`, so the stream fails and its connection is dropped rather than returned to the cache. An interrupted read is not one of those — it is retried here, and only here, because this is the level that knows whether the interruption was a cancellation. Only the checksum saves a truncated package; nothing saves a truncated unsigned index. `aept_download_cond()` adds the conditional form: it hands libfetch the validators to send and reports back the ones the server offered, and turns the `304` — which libfetch reports as a NULL return with `LIBFETCH_HTTP_NOT_MODIFIED` in `libfetch_last_error`, the way every other status arrives — into an `*unchanged` of 1 with nothing written. `aept_download()` is the unconditional wrapper.
- **api.c** — Public API implementation behind `aept.h`; **pin.c** version pinning, **autoremove.c** unneeded auto-installed packages, **clean.c** cache cleanup, **validator.c** the cache-validator record beside each index.
- **util.c** — `aept_system()` / `aept_system_offline_root()` for subprocess execution. Offline root uses `unshare(CLONE_NEWUSER)` + uid/gid mapping + chroot for non-root installs. Also the `aept_fgets_is_truncated()` / `aept_fgets_drain_line()` pair every line reader in the tree uses to drop over-long lines rather than parse them in pieces.
- **script.c** — Runs maintainer scripts (preinst/postinst/prerm/postrm) via `/bin/sh` through `aept_system_offline_root()`. No environment is set for them: with an offline root the script runs chrooted, so it already sees that root as `/` and needs no prefix variable (unlike opkg, which does not chroot and passes `$PKG_ROOT` instead).
