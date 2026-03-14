"""Load libaept via CFFI ABI mode (no compiler needed)."""

import ctypes.util
import os

from cffi import FFI

from ._cdef import CDEF, LIBC_CDEF

ffi = FFI()
ffi.cdef(CDEF)

_libc_ffi = FFI()
_libc_ffi.cdef(LIBC_CDEF)


def _find_libaept():
    # 1. Explicit env var (development use)
    path = os.environ.get("LIBAEPT_PATH")
    if path:
        return path

    # 2. System library search (LD_LIBRARY_PATH + ldconfig cache)
    path = ctypes.util.find_library("aept")
    if path:
        return path

    # 3. Bare soname fallback
    return "libaept.so.0"


lib = ffi.dlopen(_find_libaept())
libc = _libc_ffi.dlopen(None)
