#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""An HTTP server that sends a body in slow pieces.

Driven by test_signal_resume.sh.  The point is not the slowness itself
but where it puts the client: waiting in poll(2) for the next piece of a
body it has already started reading.  That is the window a signal has to
land in for the test to mean anything, and a server that answers at
loopback speed never opens it.

In chunked mode the CRLF that closes each chunk is sent as a piece of
its own, one delay after the chunk data, so the wait for a chunk's
framing is exercised as well as the wait for its bytes.

Usage: dribbleserver.py <plain|chunked> [pieces] [delay]

Prints "PORT <n>" once it is listening, then "REQ <path>" and "PIECE
<n>" as it serves, so a test can wait for the body to be under way
rather than guessing at a sleep.  Serves until it is killed.
"""

import socket
import sys
import time

mode = sys.argv[1] if len(sys.argv) > 1 else "plain"
pieces = int(sys.argv[2]) if len(sys.argv) > 2 else 20
delay = float(sys.argv[3]) if len(sys.argv) > 3 else 0.2

BODY = [b"piece-%02d\n" % i for i in range(pieces)]

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 0))
srv.listen(16)
print("PORT %d" % srv.getsockname()[1], flush=True)


def read_request(stream):
    """The request line, with the headers read and thrown away."""
    request_line = stream.readline()
    if not request_line:
        return None
    while True:
        line = stream.readline()
        if line in (b"\r\n", b"\n", b""):
            break
    parts = request_line.decode("latin-1").split()
    return parts[1] if len(parts) > 1 else "/"


while True:
    conn, _ = srv.accept()
    try:
        path = read_request(conn.makefile("rb"))
        if path is None:
            conn.close()
            continue
        print("REQ %s" % path, flush=True)

        if mode == "chunked":
            head = (
                "HTTP/1.1 200 OK\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Connection: close\r\n"
                "\r\n"
            )
        else:
            head = (
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n"
            ) % sum(len(p) for p in BODY)
        conn.sendall(head.encode("latin-1"))

        for n, piece in enumerate(BODY):
            time.sleep(delay)
            if mode == "chunked":
                # The chunk header is split from its own CRLF, so the
                # client waits in the middle of a *line* as well: a line
                # is framing too, and one read half-way through it has
                # taken bytes off the wire that a restart would lose.
                conn.sendall(b"%x" % len(piece))
                time.sleep(delay)
                conn.sendall(b"\r\n" + piece)
                print("PIECE %d" % n, flush=True)
                time.sleep(delay)
                conn.sendall(b"\r\n")
            else:
                conn.sendall(piece)
                print("PIECE %d" % n, flush=True)

        if mode == "chunked":
            conn.sendall(b"0\r\n\r\n")
        print("DONE", flush=True)
    except (BrokenPipeError, ConnectionResetError):
        print("GONE", flush=True)
    finally:
        conn.close()
