"""Helpers for marshalling strings and string lists between Python and C."""

from ._ffi import ffi, lib, libc


def str_to_c(s):
    """Convert a Python str (or None) to a C char[] (or ffi.NULL)."""
    if s is None:
        return ffi.NULL
    return ffi.new("char[]", s.encode("utf-8"))


def c_to_str(ptr):
    """Convert a C char* (or NULL) to a Python str (or None)."""
    if ptr == ffi.NULL:
        return None
    return ffi.string(ptr).decode("utf-8")


def str_list_to_c(strings):
    """Convert a list of Python strings to a C char*[] array.

    Returns (c_array, keepalive, count).  The caller must hold keepalive
    as a local variable to prevent GC while C holds pointers into it.
    """
    if not strings:
        return ffi.NULL, [], 0
    keepalive = [ffi.new("char[]", s.encode("utf-8")) for s in strings]
    c_array = ffi.new("const char *[]", keepalive)
    return c_array, keepalive, len(strings)


def c_str_array_to_list(arr, count):
    """Convert a C char** + count to a Python list[str], freeing the C memory.

    Each string element and the outer array are freed with free().
    """
    result = []
    for i in range(count):
        result.append(ffi.string(arr[i]).decode("utf-8"))
        libc.free(arr[i])
    libc.free(arr)
    return result
