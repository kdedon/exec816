#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""sendwire.py - scripted sender on an emulator serial wire.

Usage: sendwire.py SOCKET HEX

Listens on the AF_UNIX stream socket SOCKET, accepts one connection (the
emulator's --wireN=SOCKET), waits for the guest to send a byte, then
sends the bytes HEX spells (two hex digits each) and discards whatever
else arrives until the peer closes.  A '/' in HEX splits it into stages:
each stage after the first waits for one more byte from the guest.
tools/runtest.sh starts one for a test's SEND directive.
"""

import os
import socket
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    path = sys.argv[1]
    try:
        stages = [bytes.fromhex(h) for h in sys.argv[2].split('/')]
    except ValueError:
        sys.stderr.write(__doc__)
        return 2
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
        for data in stages:
            if not conn.recv(1):
                return 0
            conn.sendall(data)
        while conn.recv(4096):
            pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
