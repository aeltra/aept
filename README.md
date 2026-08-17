# aept — Aeltra Package Tool

aept is the package manager of Aeltra Linux. It installs, removes and
upgrades `.aeltra` packages, with dependency resolution via
[libsolv], archive extraction via [libarchive], and repository indexes
signed and verified with [signify]/usign. The core is a shared library,
`libaept`, with a stable C ABI and Python bindings; the `aept` command
line tool is a thin client of that library.

```sh
aept update
aept install nginx
aept remove nginx
aept autoremove
```

## Features

- Full dependency resolution (install, remove, upgrade, autoremove)
  backed by libsolv.
- Signed repository indexes. The index and its signature are fetched as
  a single object (`InPackages.gz`) and verified before anything is
  stored; there is no unsigned fallback path.
- Conditional updates: unchanged indexes are revalidated with
  `If-None-Match` / `If-Modified-Since` instead of re-downloaded.
- Offline roots: install into a target directory instead of the running
  system. Works without root privileges, using a user namespace and
  chroot, so maintainer scripts run against the target as `/`.
- Conffile handling, version pinning, directory-watch triggers and
  maintainer scripts (preinst/postinst/prerm/postrm).
- Strict transport security: for HTTPS sources, TLS peer and hostname
  verification are always on and cannot be disabled. Credentials are
  taken from URLs only, and are stripped from everything aept prints
  or stores — they reach the wire and nothing else.
- Interruptible, bounded downloads: an idle network timeout applies to
  every wait, and Ctrl-C works even mid-handshake.
- Small footprint: about 9,000 lines of C, HTTP via a pruned vendored
  fork of FreeBSD's libfetch, and only `usign`, `rm`, `diff` and
  `/bin/sh` as external tools.

## Building

Dependencies: autotools, `libarchive`, OpenSSL >= 1.1.1, `libsolv` and
`libsolvext`. At runtime, `usign` is required for signature
verification. `scdoc` and `pandoc` are only needed to rebuild the
manual.

```sh
autoreconf -i
./configure
make
make check        # test suite: C unit tests + shell integration tests
sudo make install
```

## Configuration

The default configuration file is `/etc/aept/aept.conf`. It is
line-oriented; `#` starts a comment.

```
# Repositories
src/gz base  https://repo.example.com/packages/base
src/gz extra https://repo.example.com/packages/extra

# Architectures -- the first is native, the rest are accepted below it
arch aarch64
arch all

# Options
option cache_dir       /var/cache/aept
option check_signature 1
option network_timeout 120
```

To operate on a directory instead of the running system, pass
`-o/--offline-root <dir>` or set `option offline_root`.

The full list of commands, options and configuration keys is in the
manual (see below).

## Library and Python bindings

`libaept` exposes the same operations as a C API: 31 functions declared
in [`include/aept/aept.h`](include/aept/aept.h), versioned as
`libaept.so.0`. All other symbols are hidden, and the exported set —
full declarations, not just names — is checked against a committed
baseline by the test suite, so the ABI cannot change silently.
Independent contexts can be used concurrently from different threads,
and `aept_cancel()` may be called from any thread.

Python bindings (`cffi`, package `aeltra.aept`) live in
[`python/`](python/):

```python
from aeltra.aept import Aept

with Aept() as a:
    a.set_offline_root("/srv/target")
    a.load_config("/srv/target/etc/aept/aept.conf")
    a.update()
    a.install(names=["nginx"])
```

See [`python/examples/demo.py`](python/examples/demo.py) for a fuller
example including log, display and confirm callbacks.

## Package format

A `.aeltra` package is an `ar` archive containing `debian-binary`,
`control.tar.gz` and `data.tar.gz` — the same container layout as
`.deb` and `.ipk`, so the familiar tools work on it.

## Documentation

- [docs/aept.1.md](docs/aept.1.md) — the manual, rendered for the
  browser: commands, configuration keys, offline roots, triggers,
  maintainer scripts, the package format and exit statuses. Installed
  as `man aept`; the source is [`aept.1.scd`](aept.1.scd), re-rendered
  with `make docs`.
- [`CLAUDE.md`](CLAUDE.md) — the design record: why each subsystem is
  built the way it is. Read it before changing anything.

## Layout

| path | contents |
|---|---|
| `src/` | the implementation; one file per subsystem |
| `src/libfetch/` | pruned fork of FreeBSD's libfetch — HTTP/HTTPS GET only |
| `include/aept/` | headers; `aept.h` is the public one |
| `tests/` | unit tests in C, integration tests in shell |
| `python/` | cffi bindings |
| `docs/` | the rendered manual |

## License

MIT — see [COPYING](COPYING) — except the vendored `src/libfetch/`
fork, which remains under its original BSD licence. [NOTICE](NOTICE)
carries that licence and the attributions for the third-party code
linked into the binary; ship it alongside binaries. `src/archive.c` was
originally adapted from opkg (GPL); it was rewritten from scratch and
relicensed MIT in `4f0989d`.

[libsolv]: https://github.com/openSUSE/libsolv
[libarchive]: https://www.libarchive.org/
[signify]: https://man.openbsd.org/signify
