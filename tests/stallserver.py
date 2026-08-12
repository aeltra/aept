#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A TCP listener that accepts connections and then says nothing.

Driven by test_cancel.sh and test_timeout.sh.  This is the simplest peer
that makes a transfer hang: the kernel completes the TCP handshake by
itself, so the client ends up with a connected socket and waits for a
byte that never arrives.  The peer never has to speak HTTP or TLS, which
is what lets one listener stall a plain request and a TLS handshake
alike -- and the handshake is the case that used to be unbounded.

Prints "PORT <n>" on stdout, then serves until it is killed.
"""

import socket

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
srv.listen(16)
print("PORT %d" % srv.getsockname()[1], flush=True)

held = []
while True:
    conn, _ = srv.accept()
    held.append(conn)  # keep it open, and silent
