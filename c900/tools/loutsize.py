#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""loutsize.py - print an Exec900 image's sizes and refuse one that overruns.

Usage: loutsize.py IMAGE

kboot loads text at physical 0x080000 and data at 0x080000 + roundup(text,
1K), with bss after it.  Text is one segment (at most 64 KB), data and bss
another.  The stack segment's RAM starts at the next 256-byte boundary
after bss and ends at a 1 KB boundary, 64 KB less STKSEGLOW
(platform/c900.h) after the 1 KB boundary at or below its start
(platform/crt.s).  The heap starts where it ends, so every 1 KB the image
grows past that boundary is 1 KB less heap; within it, the pool shrinks.  l.out header: 16-bit words little endian,
32-bit values high word first; l_ssize[] starts at byte 8 (SHRI, PRVI,
BSSI, SHRD, PRVD, BSSD).
"""

import os
import re
import struct
import sys

PAGE = 0x10000
BASE = 0x080000


def stksegbytes():
    h = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                     'platform', 'c900.h')
    d = dict((k, int(v, 0)) for k, v in
             re.findall(r'^#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\b',
                        open(h).read(), re.M))
    low = d.get('STKSEGLOW')
    if low is None or d.get('STKSEGPG', -1) * 256 != low or low % 1024 or \
            low == 0 or low + 0x300 > d['ROOTSTKLOW'] - d['IDLESTKSIZE'] - 1024:
        sys.exit("%s: STKSEGLOW must be STKSEGPG * 256, nonzero, 1 KB-aligned"
                 " and 1.75 KB below the idle stack" % h)
    return PAGE - low


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    b = open(path, 'rb').read(48)

    def gl(o):
        hi, lo = struct.unpack_from('<HH', b, o)
        return (hi << 16) | lo

    text = gl(8) + gl(12) + gl(16)
    data = gl(20) + gl(24)
    bss = gl(28)
    rtext = (text + 1023) // 1024 * 1024
    end = rtext + data + bss
    stack = BASE + (end + 255) // 256 * 256
    heap = stack // 1024 * 1024 + stksegbytes()
    print("%s: text %d (0x%x), data %d, bss %d; ends at phys 0x%06x, "
          "stack segment 0x%06x-0x%06x, heap from 0x%06x"
          % (path, text, text, data, bss, BASE + end, stack,
             heap - 1, heap))
    if text > PAGE:
        sys.exit("%s: text is %d bytes over one 64 KB segment"
                 % (path, text - PAGE))
    if data + bss > PAGE:
        sys.exit("%s: data and bss are %d bytes over one 64 KB segment"
                 % (path, data + bss - PAGE))


if __name__ == '__main__':
    main()
