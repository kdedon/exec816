#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""hrtext.py - read a mono HR screen (PBM) as text in the console font.

Usage: hrtext.py SCREEN.pbm [FONT.c]

Reads the 8x16 cells against devices/font8x16.c (or FONT.c), plain or
inverted, and prints one line per text row with trailing blanks removed.
A cell that matches no glyph reads as '?'.  simrun's own .screen.txt reads
the boot ROM's font instead.
"""

import os
import re
import sys


def font(path):
    src = open(path).read()
    rows = re.findall(r'\t((?:0x[0-9a-f]{2}, ){15}0x[0-9a-f]{2}),?\t', src)
    glyphs = {}
    for i, r in enumerate(rows[:95]):
        g = bytes(int(b, 16) for b in r.split(', '))
        glyphs.setdefault(g, chr(0x20 + i))
        glyphs.setdefault(bytes(b ^ 0xff for b in g), chr(0x20 + i))
    return glyphs


def pbm(path):
    data = open(path, 'rb').read()
    toks = []
    pos = 0
    while len(toks) < 3:
        m = re.compile(rb'\s*(#[^\n]*\n\s*)*(\S+)').match(data, pos)
        toks.append(m.group(2))
        pos = m.end()
    if toks[0] != b'P4':
        sys.exit('%s: not a binary PBM' % path)
    w, h = int(toks[1]), int(toks[2])
    return w, h, data[pos + 1:]


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    here = os.path.dirname(os.path.abspath(__file__))
    glyphs = font(sys.argv[2] if len(sys.argv) == 3 else
                  os.path.join(here, '..', 'devices', 'font8x16.c'))
    w, h, bits = pbm(sys.argv[1])
    stride = (w + 7) // 8
    for row in range(h // 16):
        line = []
        for col in range(stride):
            g = bytes(bits[(row * 16 + y) * stride + col] for y in range(16))
            line.append(glyphs.get(g, '?'))
        print(''.join(line).rstrip())


main()
