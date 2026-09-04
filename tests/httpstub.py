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
    # Names the connection's fate explicitly.  Apache does this; nginx
    # and Caddy do not, which is what raw_response() below is for.
    head = ["HTTP/1.1 %s" % status]
    head.extend(headers)
    head.append("Content-Length: %d" % len(body))
    head.append("Connection: %s" % ("close" if close else "keep-alive"))
    head.append("")
    head.append("")
    return "\r\n".join(head).encode("latin-1") + body


def raw_response(status, body, headers=(), version="HTTP/1.1"):
    """A response carrying exactly the headers given.

    No Connection header is added, so the client is left to apply the
    version's own default.  This is the shape nginx and Caddy actually
    produce, and the one a client that waits to be told about
    persistence gets wrong.
    """
    head = ["%s %s" % (version, status)]
    head.extend(headers)
    head.append("Content-Length: %d" % len(body))
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

        elif path == "/echo-headers":
            # Report the request headers the client chooses to send, so a
            # test can assert on aept's identity rather than on the
            # environment's.  Absent headers are reported as "(absent)",
            # which is the expected answer for Referer.
            interesting = ("user-agent", "referer")
            body = "".join(
                "%s: %s\n" % (name, headers.get(name, "(absent)"))
                for name in interesting
            ).encode("latin-1")
            send(response("200 OK", body))

        elif path == "/ok":
            send(response("200 OK", b"hello from ok\n"))
        elif path == "/close":
            # Same body, but the server declines to keep the connection.
            send(response("200 OK", b"hello from ok\n", close=True))
            return False
        elif path == "/implicit-ka":
            # HTTP/1.1 with no Connection header whatsoever -- what
            # nginx and Caddy send.  The connection is persistent by
            # RFC 9112 9.3 and the server holds it open accordingly.
            send(raw_response("200 OK", b"hello from ok\n"))
        elif path == "/ka-list":
            # The token arrives in a list, which is legal and which a
            # whole-field comparison against "keep-alive" misses.
            send(raw_response("200 OK", b"hello from ok\n",
                              ["Connection: keep-alive, TE"]))
        elif path == "/close-list":
            # "close" in a list, and -- unlike /close -- the connection
            # is deliberately left open afterwards.  A server that hung
            # up would produce one connection per request whatever the
            # client believed; holding it open makes the count report
            # the client's reading of the header and nothing else.
            send(raw_response("200 OK", b"hello from ok\n",
                              ["Connection: TE, close"]))
        elif path == "/http10":
            # An HTTP/1.0 reply with no Connection header: the default
            # there is to close, the opposite of 1.1.  Held open for
            # the same reason as /close-list.
            send(raw_response("200 OK", b"hello from ok\n",
                              version="HTTP/1.0"))
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
        elif path == "/partial206":
            # 206 with a byte range, to a client that never sent a
            # Range header: 500 bytes out of a declared 1000.
            body = b"P" * 500
            send(b"HTTP/1.1 206 Partial Content\r\n"
                 b"Content-Range: bytes 500-999/1000\r\n"
                 b"Content-Length: 500\r\n"
                 b"Connection: close\r\n\r\n" + body)
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
        elif path == "/abandon":
            # A perfectly good keep-alive response whose body, after its
            # first 64 bytes, is itself a complete and plausible
            # response.  Nothing here fails: the point is a client that
            # stops reading part-way and then reuses the connection, and
            # is answered by these leftovers rather than by its own next
            # request.  Binary filler would merely produce a protocol
            # error, which the cached-connection retry papers over.
            poison = (b"HTTP/1.1 200 OK\r\nContent-Length: 9\r\n"
                      b"Connection: keep-alive\r\n\r\npoisoned\n")
            send(response("200 OK", b"x" * 64 + poison))
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
        elif path == "/badstatus":
            # A status line that does not begin with "HTTP" at all.
            send(b"XTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nno")
            return False
        elif path == "/badversion":
            # A major version this client does not speak.
            send(b"HTTP/2.0 200 OK\r\nContent-Length: 2\r\n\r\nno")
            return False
        elif path == "/badminor":
            # ... and a minor version it does not speak either.
            send(b"HTTP/1.9 200 OK\r\nContent-Length: 2\r\n\r\nno")
            return False
        elif path == "/badcode":
            # A reply code that is not three digits.
            send(b"HTTP/1.1 2x0 Strange\r\nContent-Length: 2\r\n\r\nno")
            return False
        elif path == "/noversion":
            # No version at all, NCSA 1.5.1-style.  The parser tolerates
            # this deliberately, so it must keep working.
            send(b"HTTP 200 OK\r\nContent-Length: 15\r\n"
                 b"Connection: close\r\n\r\nversionless ok\n")
            return False
        elif path == "/chunkext":
            # A chunk-size line carrying a chunk extension.  RFC 9112
            # allows it, and the parser must stop at the semicolon
            # rather than reject the line.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            send(b"6;name=value\r\nfirst-\r\n0\r\n\r\n")
        elif path == "/chunklf":
            # A chunk-size line ended by a bare LF: the size is followed
            # by the line's end and nothing else.  Two digits, because
            # the parser insists on at least two characters before the
            # LF -- a CR's worth of slack -- and "6\n" is refused.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            send(b"06\nfirst-\r\n0\r\n\r\n")
        elif path == "/chunkemptyhdr":
            # An empty line where a chunk size belongs.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            send(b"\r\nfirst-\r\n0\r\n\r\n")
            return False
        elif path == "/chunknotrailer":
            # The chunk's data arrives whole, but the CRLF that closes
            # it never does: the server hangs up between the two.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n")
            send(b"6\r\nfirst-")
            return False
        elif path == "/chunkclen":
            # Both framings at once.  The chunked framing governs (RFC
            # 9112 6.3); the Content-Length names the payload size, so
            # a client that counts it down must still find the end of
            # the body where the terminating chunk says.
            send(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                 b"Content-Length: 19\r\n\r\n")
            send(b"6\r\nfirst-\r\n7\r\nsecond-\r\n6\r\nthird\n\r\n0\r\n\r\n")
        elif path == "/see303":
            send(response("303 See Other", b"", ["Location: /ok"]))
        elif path == "/range416":
            # A complaint about a Range header that was never sent.
            send(response("416 Range Not Satisfiable", b"nope\n"))
        elif path == "/proxy407":
            # A proxy demanding credentials when the client has already
            # sent everything it has.
            send(response("407 Proxy Authentication Required", b"nope\n"))
        elif path == "/error500":
            send(response("500 Internal Server Error", b"boom\n"))
        elif path == "/oklocation":
            # Location on a 200 is not a redirect and must be ignored.
            send(response("200 OK", b"stayed here\n", ["Location: /notfound"]))
        elif path == "/twolocations":
            # Two Location headers; the last one wins and the first
            # must not leak.
            send(response("302 Found", b"",
                          ["Location: /notfound", "Location: /ok"]))
        elif path == "/movedabs":
            # An absolute URL in Location, not just a path.
            port = self.server.server_address[1]
            send(response("302 Found", b"",
                          ["Location: http://127.0.0.1:%d/ok" % port]))
        elif path == "/movedcreds":
            # A Location that carries its own credentials; they must be
            # used for the redirected request.
            port = self.server.server_address[1]
            send(response("302 Found", b"",
                          ["Location: http://user:pass@127.0.0.1:%d/auth"
                           % port]))
        elif path == "/movedbad":
            # A Location that does not parse as a URL.
            send(response("302 Found", b"", ["Location: http://["]))
        elif path == "/movednoloc":
            # A redirect with nowhere to go.
            send(response("302 Found", b"went away\n"))
        elif path == "/longheader":
            # A header line well past the 1 KiB the line reader starts
            # with, so the buffer has to grow mid-line.
            send(response("200 OK", b"padded ok\n",
                          ["X-Padding: " + "x" * 2000]))
        elif path == "/bigetag":
            # A validator too long to store.  It must be dropped, not
            # truncated: a truncated validator could never match.
            send(response("200 OK", b"tagged ok\n",
                          ['ETag: "%s"' % ("e" * 300)]))
        elif path == "/truncated512":
            # Promises 1000 bytes and delivers exactly 512 -- one full
            # read for a caller using a 512-byte buffer -- so the
            # failure surfaces on the *next* read, when nothing has
            # been handed over yet.
            send(b"HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n")
            send(b"T" * 512)
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
