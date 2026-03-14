#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import tempfile

from aeltra.aept import Aept, Flag, LogLevel

AEPT_CONF = """\
src/gz main http://archive.aeltra.eu/dists/pasteur/core/aarch64/musl/main

arch aarch64
arch all
"""


def log_cb(level, msg):
    print(f"[{level.name}] {msg}")


def display_cb(txn):
    for name in txn.install:
        print(f"  Install: {name}")
    for name in txn.upgrade:
        print(f"  Upgrade: {name}")
    for name in txn.remove:
        print(f"  Remove:  {name}")


def confirm_cb():
    return True


with tempfile.TemporaryDirectory(prefix="aept-demo-") as tmpdir:
    print(f"Using offline root: {tmpdir}")

    conf_path = os.path.join(tmpdir, "etc", "aept", "aept.conf")
    os.makedirs(os.path.dirname(conf_path))
    with open(conf_path, "w") as f:
        f.write(AEPT_CONF)

    with Aept() as a:
        a.set_offline_root(tmpdir)
        a.set_flag(Flag.IGNORE_UID, True)
        a.load_config(conf_path)
        a.set_log_callback(log_cb)
        a.set_display_callback(display_cb)
        a.set_confirm_callback(confirm_cb)

        print("=== Updating package lists ===")
        a.update()

        print()
        print("=== Setting up base system ===")
        a.install(names=["base-files", "busybox-all-symlinks"])

        pkgs = ["cmake", "tcl", "python"]

        print()
        print("=== Installing", " ".join(pkgs), "===")
        a.install(names=pkgs)

        print()
        print("=== Removing", " ".join(pkgs), "===")
        a.remove(names=pkgs)

        print()
        print("=== Autoremove ===")
        a.autoremove()
