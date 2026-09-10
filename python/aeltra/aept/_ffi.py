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

    # 3. Bare soname fallback.
    #
    # The major here is the library's ABI number, not the release
    # version.  It is written down in three places that have to agree:
    # -version-number in src/Makefile.am, the libaept<N> package name in
    # debian/control, and this line.  debian/rules checks all three
    # against each other, because this is the last of the three
    # fallbacks: a stale value here shows up only where neither
    # $LIBAEPT_PATH nor the ldconfig cache found the library.
    return "libaept.so.1"


lib = ffi.dlopen(_find_libaept())
libc = _libc_ffi.dlopen(None)
