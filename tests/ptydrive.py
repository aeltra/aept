# ptydrive.py - drive an interactive aept run from a pseudo-terminal
#
# Copyright (C) 2026 Tobias Koch
# SPDX-License-Identifier: MIT
#
# Usage: ptydrive.py <response>[,<response>...] -- <command> [args...]
#
# Runs the command on a pty so that isatty(STDIN_FILENO) holds, and
# sends one response each time the conffile prompt appears.  A response
# of "Z" is followed by "exit\n" a moment later, to leave the shell that
# the Z option starts.
#
# Everything the command wrote is echoed to stdout.  Exits with the
# command's status, or 2 if it had to be killed for running too long.

import os
import select
import signal
import sys
import time

PROMPT = b"[default=N] ?"
TIMEOUT = 60.0

responses, command = sys.argv[1].split(","), sys.argv[sys.argv.index("--") + 1:]

pid, fd = os.forkpty()
if pid == 0:
    try:
        os.execvp(command[0], command)
    finally:
        os._exit(127)

seen = b""
output = b""
deadline = time.time() + TIMEOUT
timed_out = False

while time.time() < deadline:
    try:
        ready, _, _ = select.select([fd], [], [], 0.5)
    except OSError:
        break

    if not ready:
        continue

    try:
        chunk = os.read(fd, 4096)
    except OSError:       # the child closed the pty
        break

    if not chunk:
        break

    output += chunk
    seen += chunk

    while PROMPT in seen and responses:
        reply = responses.pop(0)
        os.write(fd, reply.encode())
        if reply == "Z":
            # The shell reads from this same pty; give it a moment to
            # start before handing it the command that ends it.
            time.sleep(0.3)
            os.write(fd, b"exit\n")
        seen = seen.split(PROMPT, 1)[1]
else:
    timed_out = True
    os.kill(pid, signal.SIGKILL)

_, status = os.waitpid(pid, 0)
os.close(fd)

sys.stdout.buffer.write(output)
sys.stdout.flush()

if timed_out:
    sys.exit(2)

sys.exit(os.waitstatus_to_exitcode(status) if os.WIFEXITED(status) else 1)
