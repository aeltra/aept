# condserver.py - an HTTP server that revalidates, and says exactly what
# it was asked
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Usage: condserver.py <directory> <mode>
#
#   both       offer an ETag and a Last-Modified
#   etag       offer an ETag only
#   lastmod    offer a Last-Modified only
#   always304  answer 304 to everything, conditional request or not
#
# Prints "PORT <n>" on stdout once it is listening, then two lines per
# request:
#
#   REQ <path> <status> inm=<If-None-Match> ims=<If-Modified-Since>
#   AUTH <path> <Authorization header, or ->
#
# with "-" for a header the client did not send.  The AUTH line is
# separate so the REQ format stays exactly what the older tests match
# against.  That log is the point
# of this server: python3's http.server does handle If-Modified-Since,
# but it emits no ETag and reports nothing about the request, so a test
# using it cannot tell a conditional request that was answered 304 from
# one that was never made.
#
# Validators are derived from the file: the ETag from its size and
# nanosecond mtime, the Last-Modified from its mtime in seconds.  If a
# test rewrites a served file, the ETag therefore always changes while
# the date may not, which is the real-world asymmetry between a strong
# validator and a weak one.
#
# Precedence follows RFC 9110 13.2.2: when If-None-Match is present,
# If-Modified-Since is ignored entirely.  The date is compared for
# equality rather than for order, which is stricter than a real server
# but exact -- a client that echoes back what it was given matches, and
# one that invents a date of its own does not.

import email.utils
import os
import socketserver
import sys
import threading

directory = sys.argv[1]
mode = sys.argv[2]

log_lock = threading.Lock()


def log(line):
    with log_lock:
        sys.stdout.write(line + "\n")
        sys.stdout.flush()


def validators(st):
    etag = '"%x-%x"' % (st.st_size, st.st_mtime_ns)
    lastmod = email.utils.formatdate(st.st_mtime, usegmt=True)
    if mode == "etag":
        return etag, None
    if mode == "lastmod":
        return None, lastmod
    return etag, lastmod


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        stream = self.request.makefile("rb")

        while True:
            request_line = stream.readline()
            if not request_line:
                return

            parts = request_line.decode("latin-1").split()
            if len(parts) < 2:
                return
            path = parts[1].split("?", 1)[0]

            headers = {}
            while True:
                header = stream.readline()
                if header in (b"\r\n", b"\n", b""):
                    break
                text = header.decode("latin-1").rstrip("\r\n")
                if ":" in text:
                    name, value = text.split(":", 1)
                    headers[name.strip().lower()] = value.strip()

            if not self.reply(path, headers):
                return

    def reply(self, path, headers):
        inm = headers.get("if-none-match")
        ims = headers.get("if-modified-since")

        def done(status):
            log("REQ %s %s inm=%s ims=%s"
                % (path, status, inm or "-", ims or "-"))
            log("AUTH %s %s"
                % (path, headers.get("authorization") or "-"))

        local = os.path.join(directory, path.lstrip("/"))
        if not os.path.isfile(local):
            done(404)
            self.send(404, b"no such document\n", [])
            return True

        if mode == "always304":
            done(304)
            self.send(304, b"", [])
            return True

        st = os.stat(local)
        etag, lastmod = validators(st)

        extra = []
        if etag:
            extra.append("ETag: " + etag)
        if lastmod:
            extra.append("Last-Modified: " + lastmod)

        if inm is not None:
            current = etag is not None and inm == etag
        elif ims is not None:
            current = lastmod is not None and ims == lastmod
        else:
            current = False

        if current:
            done(304)
            self.send(304, b"", extra)
            return True

        with open(local, "rb") as fp:
            body = fp.read()

        done(200)
        self.send(200, body, extra)
        return True

    def send(self, status, body, extra):
        reason = {200: "OK", 304: "Not Modified", 404: "Not Found"}[status]
        head = ["HTTP/1.1 %d %s" % (status, reason)]
        head.extend(extra)
        # A 304 carries no body and must not announce one: a length here
        # would leave the client waiting for bytes that never come.
        if status != 304:
            head.append("Content-Length: %d" % len(body))
        head.append("Connection: keep-alive")
        head.append("")
        head.append("")
        self.request.sendall("\r\n".join(head).encode("latin-1") + body)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


server = Server(("127.0.0.1", 0), Handler)
log("PORT %d" % server.server_address[1])

try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
