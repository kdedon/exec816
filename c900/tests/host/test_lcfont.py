# SPDX-License-Identifier: GPL-3.0-only
"""Host tests: lc.display's font table against its 8568 cell geometry."""

import os
import re
import unittest

DEV = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                   'devices')


def slurp(name):
    with open(os.path.join(DEV, name), encoding='ascii') as f:
        return f.read().replace('\r\n', '\n')


def defines(src):
    d = {}
    for m in re.finditer(r'^#define\s+(\w+)\s+(\S+)', src, re.M):
        d[m.group(1)] = m.group(2)
    return d


def value(expr, d):
    expr = re.sub(r'\b[A-Z_][A-Z0-9_]*\b', lambda m: d[m.group(0)], expr)
    return eval(expr, {'__builtins__': {}})   # noqa: S307 - our own source


class LcFont(unittest.TestCase):
    def setUp(self):
        self.lc = slurp('lc.c')
        self.d = defines(self.lc)
        self.font = slurp('font8x8.c')

    def test_table(self):
        m = re.search(r'UBYTE font8x8\[(\d+) \* (\d+)\] = \{(.*?)\};',
                      self.font, re.S)
        self.assertIsNotNone(m)
        n, h = int(m.group(1)), int(m.group(2))
        body = re.sub(r'/\*.*?\*/', '', m.group(3), flags=re.S)
        data = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', body)]
        self.assertEqual((n, h), (value('FONTN', self.d),
                                  value('FONTH', self.d)))
        self.assertEqual(len(data), n * h)
        self.assertEqual(value('FONTLO', self.d) + n, 0x7f)
        self.assertEqual(data[:h], [0] * h)            # space
        self.assertEqual(data[(ord('P') - 0x20) * h + 3], 0x7c)  # P's bar

    def test_registers(self):
        m = re.search(r'lcregs\[NREG\] = \{(.*?)\};', self.lc, re.S)
        regs = [value(x.strip(), self.d)
                for x in m.group(1).split(',') if x.strip()]
        self.assertEqual(len(regs), value('NREG', self.d))
        h = value('FONTH', self.d)
        self.assertEqual(regs[9] + 1, h)               # R9: lines - 1
        self.assertEqual(regs[23], h)                  # R23: lines shown
        # Below 16 lines the chip reads 16-byte glyphs.
        self.assertLess(regs[9], 16)
        self.assertEqual(value('GLYPH', self.d), 16)
        self.assertEqual(value('V_FONTEND - V_FONT', self.d), 256 * 16)

    def test_interlace(self):
        """640x400: lcil makes 16-line cells for font8x16, still read
        as 16-byte glyphs, with the vertical timing unchanged."""
        m = re.search(r'lcil\[\] = \{(.*?)\};', self.lc, re.S)
        il = [value(x.strip(), self.d)
              for x in m.group(1).split(',') if x.strip()]
        il = dict(zip(il[0::2], il[1::2]))
        self.assertEqual(il, {8: 3, 9: 15, 11: 15, 23: 16, 29: 15})
        self.assertEqual(value('CUR_ON16', self.d), 14)     # from line 14
        self.assertEqual(value('ILH', self.d), 400)
        f = re.search(r'UBYTE font8x16\[(\d+) \* (\d+)\]', slurp('font8x16.c'))
        self.assertEqual((int(f.group(1)), int(f.group(2))),
                         (value('FONTN', self.d), il[9] + 1))
        self.assertEqual(il[9] + 1, 2 * value('FONTH', self.d))
        self.assertLess(il[9], 16)


if __name__ == '__main__':
    unittest.main()
