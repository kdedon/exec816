#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""echowire.py - loopback peer for an emulator serial wire.

Usage: echowire.py SOCKET

Listens on the AF_UNIX stream socket SOCKET, accepts one connection (the
emulator's --wireN=SOCKET) and sends back every byte it receives until the
peer closes.  tools/runtest.sh starts one per port a test's WIRE directive
names.
"""

import os
import socket
import sys


def main():
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    path = sys.argv[1]
    if os.path.exists(path):
        os.unlink(path)
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(path)
    srv.listen(1)
    srv.settimeout(30)
    try:
        conn, _ = srv.accept()
    except socket.timeout:
        return 1
    finally:
        srv.close()
        os.unlink(path)
    with conn:
        while True:
            data = conn.recv(4096)
            if not data:
                return 0
            conn.sendall(data)


if __name__ == '__main__':
    sys.exit(main())
