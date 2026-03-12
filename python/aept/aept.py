"""Pythonic wrapper around the libaept C API."""

import sys
from dataclasses import dataclass
from enum import IntEnum
from typing import Callable, List, Optional

from ._ffi import ffi, lib
from ._marshalling import c_str_array_to_list, c_to_str, str_list_to_c, str_to_c


class AeptError(Exception):
    """Raised when a libaept function returns an error."""


# --- Enums ----------------------------------------------------------------

class Flag(IntEnum):
    FORCE_DEPENDS   = lib.AEPT_FLAG_FORCE_DEPENDS
    DOWNLOAD_ONLY   = lib.AEPT_FLAG_DOWNLOAD_ONLY
    NOACTION        = lib.AEPT_FLAG_NOACTION
    ALLOW_DOWNGRADE = lib.AEPT_FLAG_ALLOW_DOWNGRADE
    REINSTALL       = lib.AEPT_FLAG_REINSTALL
    NO_CACHE        = lib.AEPT_FLAG_NO_CACHE
    FORCE_CONFNEW   = lib.AEPT_FLAG_FORCE_CONFNEW
    FORCE_CONFOLD   = lib.AEPT_FLAG_FORCE_CONFOLD
    PURGE           = lib.AEPT_FLAG_PURGE
    NON_INTERACTIVE = lib.AEPT_FLAG_NON_INTERACTIVE
    CHECK_SIGNATURE = lib.AEPT_FLAG_CHECK_SIGNATURE
    IGNORE_UID      = lib.AEPT_FLAG_IGNORE_UID


class LogLevel(IntEnum):
    ERROR   = lib.AEPT_LOG_ERROR
    WARNING = lib.AEPT_LOG_WARNING
    INFO    = lib.AEPT_LOG_INFO
    DEBUG   = lib.AEPT_LOG_DEBUG


# --- Dataclasses ----------------------------------------------------------

@dataclass
class PkgEntry:
    name: str
    version: str
    summary: str
    installed: bool
    upgradable: bool


@dataclass
class PkgInfo:
    name: str
    version: str
    architecture: Optional[str]
    installed_size: int
    depends: Optional[str]
    pre_depends: Optional[str]
    recommends: Optional[str]
    suggests: Optional[str]
    provides: Optional[str]
    conflicts: Optional[str]
    replaces: Optional[str]
    homepage: Optional[str]
    filename: Optional[str]
    summary: Optional[str]
    description: Optional[str]
    is_installed: bool


@dataclass
class Transaction:
    install: List[str]
    upgrade: List[str]
    reinstall: List[str]
    remove: List[str]


# --- Helpers --------------------------------------------------------------

def _txn_to_python(txn):
    """Convert a C aept_transaction_t* to a Python Transaction."""
    def _read_list(arr, count):
        return [ffi.string(arr[i]).decode("utf-8") for i in range(count)]

    return Transaction(
        install=_read_list(txn.install, txn.n_install),
        upgrade=_read_list(txn.upgrade, txn.n_upgrade),
        reinstall=_read_list(txn.reinstall, txn.n_reinstall),
        remove=_read_list(txn.remove, txn.n_remove),
    )


# --- Main class -----------------------------------------------------------

class Aept:
    """Context manager wrapping the libaept C library.

    Usage::

        with Aept() as a:
            a.load_config()
            a.update()
            a.install(names=["hello"])
    """

    def __init__(self):
        self._closed = False
        self._log_cb_handle = None
        self._display_cb_handle = None
        self._confirm_cb_handle = None
        self._pending_exc = None
        self._ctx = lib.aept_init()
        if self._ctx == ffi.NULL:
            raise AeptError("aept_init() failed")

    def __enter__(self):
        return self

    def __del__(self):
        self.close()

    def __exit__(self, *exc):
        self.close()
        return False

    def close(self):
        """Release all libaept resources.  Idempotent."""
        if self._closed:
            return
        self._closed = True
        lib.aept_cleanup(self._ctx)
        self._ctx = ffi.NULL
        self._log_cb_handle = None
        self._display_cb_handle = None
        self._confirm_cb_handle = None
        self._pending_exc = None

    def _call(self, rc, msg="libaept error"):
        """Check for pending callback exceptions, then the C return code."""
        exc_info = self._pending_exc
        if exc_info is not None:
            self._pending_exc = None
            raise exc_info[1].with_traceback(exc_info[2])
        if rc == -1:
            raise AeptError(msg)
        return rc

    # --- Configuration ----------------------------------------------------

    def load_config(self, path: Optional[str] = None):
        self._call(lib.aept_load_config(self._ctx, str_to_c(path)),
                   "aept_load_config() failed")

    def set_offline_root(self, path: Optional[str]):
        lib.aept_set_offline_root(self._ctx, str_to_c(path))

    def set_verbosity(self, level: int):
        lib.aept_set_verbosity(self._ctx, int(level))

    # --- Flags ------------------------------------------------------------

    def set_flag(self, flag: int, value: bool):
        lib.aept_set_flag(self._ctx, int(flag), int(value))

    def get_flag(self, flag: int) -> bool:
        return bool(lib.aept_get_flag(self._ctx, int(flag)))

    # --- Callbacks --------------------------------------------------------

    def set_log_callback(self, fn: Optional[Callable[[LogLevel, str], None]]):
        """Set a Python log callback, or None to clear.

        fn signature: fn(level: LogLevel, msg: str)
        """
        if fn is None:
            lib.aept_set_log_fn(self._ctx, ffi.NULL, ffi.NULL)
            self._log_cb_handle = None
            return

        @ffi.callback("void(int, const char *, void *)")
        def _cb(level, msg, _userdata):
            try:
                fn(LogLevel(level), ffi.string(msg).decode("utf-8"))
            except Exception:
                if self._pending_exc is None:
                    self._pending_exc = sys.exc_info()

        self._log_cb_handle = _cb
        lib.aept_set_log_fn(self._ctx, _cb, ffi.NULL)

    def set_display_callback(self, fn: Optional[Callable[[Transaction], None]]):
        """Set a Python display callback, or None to clear.

        fn signature: fn(txn: Transaction)
        """
        if fn is None:
            lib.aept_set_display_fn(self._ctx, ffi.NULL, ffi.NULL)
            self._display_cb_handle = None
            return

        @ffi.callback("void(const aept_transaction_t *, void *)")
        def _cb(txn, _userdata):
            try:
                fn(_txn_to_python(txn))
            except Exception:
                if self._pending_exc is None:
                    self._pending_exc = sys.exc_info()

        self._display_cb_handle = _cb
        lib.aept_set_display_fn(self._ctx, _cb, ffi.NULL)

    def set_confirm_callback(self, fn: Optional[Callable[[], bool]]):
        """Set a Python confirm callback, or None to clear.

        fn signature: fn() -> bool  (True to proceed, False to abort)
        """
        if fn is None:
            lib.aept_set_confirm_fn(self._ctx, ffi.NULL, ffi.NULL)
            self._confirm_cb_handle = None
            return

        @ffi.callback("int(void *)")
        def _cb(_userdata):
            try:
                return 1 if fn() else 0
            except Exception:
                if self._pending_exc is None:
                    self._pending_exc = sys.exc_info()
                return 0

        self._confirm_cb_handle = _cb
        lib.aept_set_confirm_fn(self._ctx, _cb, ffi.NULL)

    # --- Cancellation -----------------------------------------------------

    def cancel(self):
        """Signal cancellation (async-signal-safe)."""
        lib.aept_cancel(self._ctx)

    # --- Mutating operations ----------------------------------------------

    def update(self):
        self._call(lib.aept_update(self._ctx), "aept_update() failed")

    def install(self, names: Optional[List[str]] = None,
                local_paths: Optional[List[str]] = None):
        c_names, ka1, n_names = str_list_to_c(names or [])
        c_paths, ka2, n_paths = str_list_to_c(local_paths or [])
        self._call(lib.aept_install(self._ctx, c_names, n_names,
                                    c_paths, n_paths),
               "aept_install() failed")

    def upgrade(self):
        self._call(lib.aept_upgrade(self._ctx), "aept_upgrade() failed")

    def remove(self, names: List[str]):
        c_names, ka, count = str_list_to_c(names)
        self._call(lib.aept_remove(self._ctx, c_names, count),
                   "aept_remove() failed")

    def autoremove(self):
        self._call(lib.aept_autoremove(self._ctx), "aept_autoremove() failed")

    def clean(self):
        self._call(lib.aept_clean(self._ctx), "aept_clean() failed")

    def pin(self, specs: List[str]):
        c_specs, ka, count = str_list_to_c(specs)
        self._call(lib.aept_pin(self._ctx, c_specs, count),
                   "aept_pin() failed")

    def unpin(self, names: List[str]):
        c_names, ka, count = str_list_to_c(names)
        self._call(lib.aept_unpin(self._ctx, c_names, count),
                   "aept_unpin() failed")

    def mark_auto(self, names: List[str]):
        c_names, ka, count = str_list_to_c(names)
        self._call(lib.aept_mark_auto(self._ctx, c_names, count),
                   "aept_mark_auto() failed")

    def mark_manual(self, names: List[str]):
        c_names, ka, count = str_list_to_c(names)
        self._call(lib.aept_mark_manual(self._ctx, c_names, count),
                   "aept_mark_manual() failed")

    def mark_manual_all(self):
        self._call(lib.aept_mark_manual_all(self._ctx),
                   "aept_mark_manual_all() failed")

    # --- Query: list ------------------------------------------------------

    def list_packages(self, pattern: Optional[str] = None, *,
                      installed: bool = False,
                      upgradable: bool = False) -> List[PkgEntry]:
        out = ffi.new("aept_pkg_list_t *")
        try:
            self._call(lib.aept_list(self._ctx, str_to_c(pattern),
                                     int(installed), int(upgradable), out),
                   "aept_list() failed")
            result = []
            for i in range(out.count):
                e = out.entries[i]
                result.append(PkgEntry(
                    name=c_to_str(e.name),
                    version=c_to_str(e.version),
                    summary=c_to_str(e.summary),
                    installed=bool(e.installed),
                    upgradable=bool(e.upgradable),
                ))
            return result
        finally:
            lib.aept_pkg_list_free(out)

    # --- Query: show ------------------------------------------------------

    def show(self, name: str) -> Optional[PkgInfo]:
        out = ffi.new("aept_pkg_info_t *")
        try:
            rc = self._call(lib.aept_show(self._ctx, str_to_c(name), out),
                        "aept_show() failed")
            if rc == 1:
                return None
            return PkgInfo(
                name=c_to_str(out.name),
                version=c_to_str(out.version),
                architecture=c_to_str(out.architecture),
                installed_size=out.installed_size,
                depends=c_to_str(out.depends),
                pre_depends=c_to_str(out.pre_depends),
                recommends=c_to_str(out.recommends),
                suggests=c_to_str(out.suggests),
                provides=c_to_str(out.provides),
                conflicts=c_to_str(out.conflicts),
                replaces=c_to_str(out.replaces),
                homepage=c_to_str(out.homepage),
                filename=c_to_str(out.filename),
                summary=c_to_str(out.summary),
                description=c_to_str(out.description),
                is_installed=bool(out.is_installed),
            )
        finally:
            lib.aept_pkg_info_free(out)

    # --- Query: files / owns / architectures ------------------------------

    def files(self, name: str) -> Optional[List[str]]:
        paths_out = ffi.new("char ***")
        count_out = ffi.new("int *")
        rc = self._call(lib.aept_files(self._ctx, str_to_c(name),
                                       paths_out, count_out),
                     "aept_files() failed")
        if rc == 1:
            return None
        return c_str_array_to_list(paths_out[0], count_out[0])

    def owns(self, path: str) -> Optional[List[str]]:
        owners_out = ffi.new("char ***")
        count_out = ffi.new("int *")
        rc = self._call(lib.aept_owns(self._ctx, str_to_c(path),
                                      owners_out, count_out),
                     "aept_owns() failed")
        if rc == 1:
            return None
        return c_str_array_to_list(owners_out[0], count_out[0])

    def architectures(self) -> List[str]:
        archs_out = ffi.new("char ***")
        count_out = ffi.new("int *")
        self._call(lib.aept_architectures(self._ctx, archs_out, count_out),
               "aept_architectures() failed")
        return c_str_array_to_list(archs_out[0], count_out[0])
