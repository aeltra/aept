# tlsserver.py - a TLS-wrapped file server for the rejection tests
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Usage: tlsserver.py <directory> <certfile> <keyfile>
#
# Serves <directory> over HTTPS with the given certificate.  Prints
# "PORT <n>" once it is listening.  The clients this server exists for
# are expected to *refuse* the handshake, so handshake errors are
# routine and silently ignored.

import http.server
import ssl
import sys

directory = sys.argv[1]

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(sys.argv[2], sys.argv[3])


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=directory, **kwargs)

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def handle_error(self, request, client_address):
        pass  # a refused handshake is this server's purpose


server = Server(("127.0.0.1", 0), Handler)
server.socket = ctx.wrap_socket(server.socket, server_side=True)
print("PORT %d" % server.server_address[1], flush=True)

try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
