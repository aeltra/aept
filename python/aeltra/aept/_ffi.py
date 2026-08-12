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
    # debian/control, and this line.  debian/rules checks the first two
    # against each other; nothing checks this one, so it has to be
    # updated by hand when the major is bumped -- and it fails only on a
    # system where neither $LIBAEPT_PATH nor the ldconfig cache found
    # the library, which is exactly where it is hardest to diagnose.
    return "libaept.so.0"


lib = ffi.dlopen(_find_libaept())
libc = _libc_ffi.dlopen(None)
