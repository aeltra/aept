# httpstub.py - an HTTP server that produces exactly the responses the
# characterisation tests need
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Usage: httpstub.py <connection-count-file>
#
# Prints "PORT <n>" on stdout once it is listening, then serves until
# killed.  Speaks HTTP/1.1 with keep-alive, so a client that reuses
# connections is visible as a connection count lower than the request
# count.  The count of accepted TCP connections is rewritten to the
# named file on every accept.
#
# python3's http.server cannot express most of what is tested here —
# a chunked body, a reply that lies about its length, a reply with no
# length at all — so the responses are written as raw bytes.

import socketserver
import sys
import threading

count_path = sys.argv[1]
connections = 0
count_lock = threading.Lock()

LARGE = b"0123456789abcdef" * 65536          # 1 MiB
BINARY = bytes([0, 1, 2, 255, 10, 0, 65, 66, 0, 254])
CHUNKS = [b"first-", b"second-", b"third\n"]


def record_connection():
    global connections
    with count_lock:
        connections += 1
        n = connections
    with open(count_path, "w") as fp:
        fp.write("%d\n" % n)


def response(status, body, headers=(), close=False):
    head = ["HTTP/1.1 %s" % status]
    head.extend(headers)
    head.append("Content-Length: %d" % len(body))
    # libfetch only reuses a connection when the server says so
    # explicitly; HTTP/1.1's implicit default is not enough for it.
    # nginx does send this, so it is the realistic case.
    head.append("Connection: %s" % ("close" if close else "keep-alive"))
    head.append("")
    head.append("")
    return "\r\n".join(head).encode("latin-1") + body


def chunked_response():
    head = (
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
    ).encode("latin-1")
    body = b""
    for chunk in CHUNKS:
        body += b"%x\r\n" % len(chunk) + chunk + b"\r\n"
    body += b"0\r\n\r\n"
    return head + body


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        record_connection()
        stream = self.request.makefile("rb")

        while True:
            request_line = stream.readline()
            if not request_line:
                return

            parts = request_line.decode("latin-1").split()
            if len(parts) < 2:
                return
            target = parts[1]

            headers = {}
            while True:
                header = stream.readline()
                if header in (b"\r\n", b"\n", b""):
                    break
                text = header.decode("latin-1").rstrip("\r\n")
                if ":" in text:
                    name, value = text.split(":", 1)
                    headers[name.strip().lower()] = value.strip()

            # A client going through a proxy sends the whole URL as the
            # request target rather than just the path.  That is the
            # only way to tell, from the server side, that the proxy
            # path was taken.  The body also reports whether the client
            # authenticated itself to the proxy, and with what.
            if target.lower().startswith("http://"):
                got = headers.get("proxy-authorization", "")
                if not got:
                    body = b"reached via proxy\n"
                elif got == "Basic dXNlcjpwYXNz":     # user:pass
                    body = b"reached via authenticated proxy\n"
                else:
                    body = b"proxy credentials rejected\n"
                self.request.sendall(response("200 OK", body))
                continue

            path = target.split("?", 1)[0]

            if not self.reply(path, headers):
                return

    def reply(self, path, headers):
        """Write the response for path.  Returns False to close."""
        send = self.request.sendall

        if path == "/auth":
            # user:pass, base64-encoded, is dXNlcjpwYXNz
            got = headers.get("authorization", "")
            if not got:
                send(response("401 Unauthorized", b"who are you\n",
                              ['WWW-Authenticate: Basic realm="test"']))
            elif got == "Basic dXNlcjpwYXNz":
                send(response("200 OK", b"authorised\n"))
            else:
                send(response("403 Forbidden", b"wrong credentials\n"))
            return True

        elif path == "/ok":
            send(response("200 OK", b"hello from ok\n"))
        elif path == "/close":
            # Same body, but the server declines to keep the connection.
            send(response("200 OK", b"hello from ok\n", close=True))
            return False
        elif path == "/empty":
            send(response("200 OK", b""))
        elif path == "/binary":
            send(response("200 OK", BINARY))
        elif path == "/large":
            send(response("200 OK", LARGE))
        elif path == "/chunked":
            send(chunked_response())
        elif path == "/notfound":
            send(response("404 Not Found", b"nope\n"))
        elif path == "/moved301":
            send(response("301 Moved Permanently", b"", ["Location: /ok"]))
        elif path == "/found302":
            send(response("302 Found", b"", ["Location: /ok"]))
        elif path == "/temp307":
            send(response("307 Temporary Redirect", b"", ["Location: /ok"]))
        elif path == "/loop":
            send(response("302 Found", b"", ["Location: /loop"]))
        elif path == "/truncated":
            # Claims a kilobyte, sends ten bytes, hangs up.
            send(b"HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n")
            send(b"0123456789")
            return False
        elif path == "/truncated-ka":
            # The same, but having announced keep-alive first: a server
            # that meant to hold the connection open and then died.
            send(b"HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n"
                 b"Connection: keep-alive\r\n\r\n")
            send(b"0123456789")
            return False
        elif path == "/chunkbad":
            # Chunked with a corrupt frame, built to be as convincing as
            # possible to a client that does not check the framing: the
            # chunk is followed by "@@" where CRLF belongs, but a valid
            # terminating chunk follows right after, so a client that
            # merely shrugs at the bad trailer sees the stream end
            # cleanly with a six-byte body.
            #
            # Unlike the truncation cases the connection then stays
            # open, and what is already on the wire is a complete,
            # plausible response.  Whoever reuses this connection is
            # answered by that instead of by their own request.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                 b"Connection: keep-alive\r\n\r\n")
            send(b"6\r\nfirst-@@0\r\n\r\n")
            send(b"HTTP/1.1 200 OK\r\nContent-Length: 9\r\n"
                 b"Connection: keep-alive\r\n\r\npoisoned\n")
            return True
        elif path == "/chunktrunc":
            # Chunked, and the stream stops in the middle of a chunk:
            # sixteen bytes are promised, six arrive, and there is no
            # terminating zero-length chunk.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            send(b"10\r\nfirst-")
            return False
        elif path == "/noclen":
            # No length at all: the body ends when the connection does.
            send(b"HTTP/1.1 200 OK\r\n\r\n")
            send(b"body without a content length\n")
            return False
        else:
            send(response("404 Not Found", b"unknown path\n"))

        return True


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


server = Server(("127.0.0.1", 0), Handler)
# The count file must exist even before the first request.
with open(count_path, "w") as fp:
    fp.write("0\n")

sys.stdout.write("PORT %d\n" % server.server_address[1])
sys.stdout.flush()

try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
