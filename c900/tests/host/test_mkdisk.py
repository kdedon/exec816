# SPDX-License-Identifier: MIT
"""Host tests for the exec partition size in tools/mkdisk.py."""

import os
import re
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                     'tools')
sys.path.insert(0, TOOLS)
import cohfs                                                # noqa: E402
import mkdisk                                               # noqa: E402

TOOL = os.path.join(TOOLS, 'mkdisk.py')
BS = cohfs.BS
EXEC = 136


class ExecSize(unittest.TestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.kboot = self.put('kboot', b'\x5a' * 4000)
        self.kernel = self.put('exec900.sys', b'\xa5' * 60000)

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, name, data):
        path = os.path.join(self.dir, name)
        with open(path, 'wb') as f:
            f.write(data)
        return path

    def mkdisk(self, *args):
        out = os.path.join(self.dir, 'disk.bin')
        p = subprocess.run([sys.executable, '-B', TOOL, '--kboot=' + self.kboot]
                           + list(args) + [out, self.kernel],
                           capture_output=True, text=True)
        if p.returncode != 0:
            return p.stderr, None
        with open(out, 'rb') as f:
            return p.stderr, f.read()

    def layout(self, img, blocks):
        """Check the exec filesystem's size and fsck, and the pattern
        region right after it."""
        sb = img[(EXEC + 1) * BS:(EXEC + 2) * BS]
        self.assertEqual(cohfs.u32(sb, 2), blocks)
        errs, warns = cohfs.check(img, EXEC, strict=True)
        self.assertEqual(errs, [])
        pat = (EXEC + blocks) * BS
        self.assertEqual(img[pat:pat + 4], bytes(4))
        self.assertEqual(img[pat + BS:pat + BS + 4], (1).to_bytes(4, 'big'))
        return cohfs.u16(sb, 0)

    def inode(self, img, ino):
        o = (EXEC + 2) * BS + (ino - 1) * 64
        return img[o:o + 64]

    def entries(self, img, ino):
        """A small directory's entries as {name: inode}."""
        i = self.inode(img, ino)
        b = i[12] << 16 | i[14] << 8 | i[13]
        d = img[(EXEC + b) * BS:(EXEC + b) * BS + cohfs.u32(i, 8)]
        return {d[o + 2:o + 16].rstrip(b'\0').decode(): cohfs.u16(d, o)
                for o in range(0, len(d), 16) if cohfs.u16(d, o)}

    def test_default_keeps_1024(self):
        _, img = self.mkdisk()
        self.assertEqual(self.layout(img, 1024), 12)

    def test_default_grows_with_contents(self):
        big = self.put('big', bytes(range(256)) * 2400)       # 1200 blocks
        _, img = self.mkdisk('--file=' + big)
        blocks = cohfs.u32(img[(EXEC + 1) * BS:], 2)
        self.assertGreater(blocks, 1024)
        self.assertEqual(blocks % 64, 0)
        self.layout(img, blocks)

    def test_many_files_grow_inodes(self):
        puts = ['--put=C/F%d=%s' % (i, self.put('f%d' % i, b'x'))
                for i in range(100)]
        _, img = self.mkdisk(*puts)
        self.assertGreater(self.layout(img, 1024), 12)

    def test_mkdir(self):
        f = self.put('f', b'x')
        _, img = self.mkdisk('--mkdir=T/Empty', '--put=C/F=' + f,
                             '--mkdir=C')
        self.layout(img, 1024)
        out = os.path.join(self.dir, 'disk.bin')
        p = subprocess.run([sys.executable, '-B',
                            os.path.join(TOOLS, 'cohfs.py'),
                            '--check', '--strict', out, str(EXEC)],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        root = self.entries(img, cohfs.ROOTIN)
        t = self.entries(img, root['T'])
        self.assertEqual(sorted(t), ['.', '..', 'Empty'])
        self.assertEqual(sorted(self.entries(img, t['Empty'])), ['.', '..'])
        empty, parent = self.inode(img, t['Empty']), self.inode(img, root['T'])
        self.assertEqual(empty[:2], parent[:2])             # mode
        self.assertEqual(empty[52:], parent[52:])           # times
        self.assertIn('F', self.entries(img, root['C']))
        _, img = self.mkdisk('--exec=1088', '--mkdir=T/Empty')
        self.layout(img, 1088)

    def test_mkdir_over_file(self):
        f = self.put('f', b'x')
        err, img = self.mkdisk('--put=C/F=' + f, '--mkdir=C/F/D')
        self.assertIsNone(img)
        self.assertIn('in the way', err)

    def test_explicit(self):
        _, img = self.mkdisk('--exec=2000')
        self.layout(img, 2000)

    def test_explicit_too_small(self):
        err, img = self.mkdisk('--exec=100')
        self.assertIsNone(img)
        self.assertIn('needs', err)

    def test_module_also_file(self):
        m = self.put('mod', b'm')
        _, img = self.mkdisk('--file=' + m, '--module=' + m)
        self.layout(img, 1024)
        root = self.entries(img, cohfs.ROOTIN)
        self.assertIn('mod', root)
        self.assertIn('Modules', root)
        err, img = self.mkdisk('--file=' + m, '--file=' + m)
        self.assertIsNone(img)
        self.assertIn('repeated', err)


def be16(d, o):
    return int.from_bytes(d[o:o + 2], 'big')


class StackConfig(unittest.TestCase):
    """--stack and --rootstack patch the kernel copy's kstkcfg words."""

    CFG = b'E9STACKK' + bytes(2) + b'ROOT' + b'\xf0\x00'
    put = ExecSize.put
    mkdisk = ExecSize.mkdisk
    tearDown = ExecSize.tearDown

    def setUp(self):
        ExecSize.setUp(self)
        self.kernel = self.put('exec900.sys',
                               b'\xa5' * 100 + self.CFG + b'\x5a' * 100)

    def cfg(self, img):
        k = cohfs.read_tree(img, EXEC)['exec900.sys']
        at = k.find(b'E9STACKK')
        self.assertEqual(len(k), 200 + len(self.CFG))
        return be16(k, at + 8), k[at + 10:at + 14], be16(k, at + 14)

    def test_default_unchanged(self):
        _, img = self.mkdisk()
        self.assertEqual(self.cfg(img), (0, b'ROOT', 0xf000))

    def test_rootstack(self):
        _, img = self.mkdisk('--rootstack=1')
        self.assertEqual(self.cfg(img), (0, b'ROOT', 0xfc00))
        _, img = self.mkdisk('--stack=45', '--rootstack=16')
        self.assertEqual(self.cfg(img), (45, b'ROOT', 0xc000))
        _, img = self.mkdisk('--stack=48')
        self.assertEqual(self.cfg(img), (48, b'ROOT', 0xf000))

    def test_rootstack_range(self):
        for v in ('0', '17', 'x'):
            err, img = self.mkdisk('--rootstack=' + v)
            self.assertIsNone(img, v)

    def test_old_image(self):
        """An image whose stack word has no root tag after it takes
        --stack but refuses --rootstack."""
        self.kernel = self.put('exec900.sys', b'\xa5' * 100 + b'E9STACKK' +
                               bytes(2) + b'\x31\x00\x02\xa6' + bytes(96))
        _, img = self.mkdisk('--stack=40')
        k = cohfs.read_tree(img, EXEC)['exec900.sys']
        self.assertEqual(be16(k, 108), 40)
        self.assertEqual(k[110:114], b'\x31\x00\x02\xa6')
        err, img = self.mkdisk('--rootstack=1')
        self.assertIsNone(img)
        self.assertIn('no root stack tag', err)


class HRMode(unittest.TestCase):
    """--hrmode patches the kernel copy's khrcfg word with a CDM_ code."""

    CFG = b'E9HRMODE' + bytes(2)
    put = ExecSize.put
    mkdisk = ExecSize.mkdisk
    tearDown = ExecSize.tearDown

    def setUp(self):
        ExecSize.setUp(self)
        self.kernel = self.put('exec900.sys',
                               b'\xa5' * 100 + self.CFG + b'\x5a' * 100)

    def mode(self, *opts):
        err, img = self.mkdisk(*opts)
        self.assertIsNotNone(img, err)
        k = cohfs.read_tree(img, EXEC)['exec900.sys']
        self.assertEqual(len(k), 200 + len(self.CFG))
        self.assertEqual(k[:100] + k[110:], b'\xa5' * 100 + b'\x5a' * 100)
        return be16(k, k.find(b'E9HRMODE') + 8)

    def test_modes(self):
        self.assertEqual(self.mode(), 0)
        for v, m in (('auto', 0), ('800', 1), ('1024', 2), ('rgbi', 3)):
            self.assertEqual(self.mode('--hrmode=' + v), m, v)

    def test_bad(self):
        for v in ('', '640', 'RGBI', 'x'):
            err, img = self.mkdisk('--hrmode=' + v)
            self.assertIsNone(img, v)

    def test_no_tag(self):
        self.kernel = self.put('exec900.sys', b'\xa5' * 200)
        self.assertIsNotNone(self.mkdisk()[1])
        err, img = self.mkdisk('--hrmode=1024')
        self.assertIsNone(img)
        self.assertIn('no single HR mode tag', err)


class LCMode(unittest.TestCase):
    """--lcmode sets the height of the FBMode after the one holding the
    E9LCMODE tag."""

    TAG = b'E9LCMODE' + bytes(18)
    MODE = (640).to_bytes(2, 'big') + (200).to_bytes(2, 'big') + \
        b'\x00\x01' * 3 + bytes(16)
    put = ExecSize.put
    mkdisk = ExecSize.mkdisk
    tearDown = ExecSize.tearDown

    def setUp(self):
        ExecSize.setUp(self)
        self.kernel = self.put('exec900.sys', b'\xa5' * 100 + self.TAG +
                               self.MODE + b'\x5a' * 100)

    def height(self, *opts):
        err, img = self.mkdisk(*opts)
        self.assertIsNotNone(img, err)
        k = cohfs.read_tree(img, EXEC)['exec900.sys']
        at = 100 + len(self.TAG)
        self.assertEqual(k[:at + 2], b'\xa5' * 100 + self.TAG + b'\x02\x80')
        self.assertEqual(k[at + 4:], self.MODE[4:] + b'\x5a' * 100)
        return be16(k, at + 2)

    def test_modes(self):
        self.assertEqual(self.height(), 200)
        self.assertEqual(self.height('--lcmode=400'), 400)
        self.assertEqual(self.height('--lcmode=200'), 200)

    def test_bad(self):
        for v in ('', '640', '0', 'x', '400i'):
            err, img = self.mkdisk('--lcmode=' + v)
            self.assertIsNone(img, v)
            self.assertIn('not 200 or 400', err)

    def test_no_tag(self):
        self.kernel = self.put('exec900.sys', b'\xa5' * 200)
        self.assertIsNotNone(self.mkdisk()[1])
        err, img = self.mkdisk('--lcmode=400')
        self.assertIsNone(img)
        self.assertIn('no single LC mode tag', err)

    def test_no_mode(self):
        self.kernel = self.put('exec900.sys', b'\xa5' * 100 + self.TAG +
                               b'\x5a' * 100)
        err, img = self.mkdisk('--lcmode=400')
        self.assertIsNone(img)
        self.assertIn('no LC mode after its tag', err)

    def test_kernel_source(self):
        """console.c's tag entry spells E9LCMODE and the LC mode follows."""
        src = open(os.path.join(TOOLS, '..', 'devices', 'console.c')).read()
        words = re.search(r'\{ (0x[0-9a-f]{4}), (0x[0-9a-f]{4}), '
                          r'(0x[0-9a-f]{4}), (0x[0-9a-f]{4}) \},\s*'
                          r'\{ 640, 200, ', src)
        self.assertIsNotNone(words)
        self.assertEqual(b''.join(int(w, 16).to_bytes(2, 'big')
                                  for w in words.groups()),
                         mkdisk.LCTAG)


class LongNames(unittest.TestCase):
    """--lnames: a standard boot partition and a long-name system
    partition (cohfs.py's variant) in slot 13."""

    setUp, tearDown, put, mkdisk = (ExecSize.setUp, ExecSize.tearDown,
                                    ExecSize.put, ExecSize.mkdisk)

    def test_layout(self):
        f = self.put('f', b'icon' * 300)
        m = self.put('mod', b'm')
        long = 'Commodities-Exchange.info'
        _, img = self.mkdisk('--lnames', '--module=' + m,
                             '--put=Prefs/%s=%s' % (long, f),
                             '--put=C/ClickToFront.info=' + f,
                             '--mkdir=T/A-directory-name-of-30-bytes!')
        boot = cohfs.read_tree(img, EXEC)
        self.assertEqual(sorted(boot), ['Modules', 'exec900.sys', 'mod'])
        sb = img[(EXEC + 1) * BS:(EXEC + 2) * BS]
        self.assertEqual(cohfs.u32(sb, 2), 1024)
        self.assertEqual(sb[484:490], b'Boot\0\0')
        self.assertEqual(cohfs.check(img, EXEC, strict=True), ([], []))
        base = EXEC + 1024
        sb = img[(base + 1) * BS:(base + 2) * BS]
        self.assertEqual(cohfs.u32(sb, 2), 0)               # COHERENT refuses
        self.assertEqual(sb[500:504], b'E9LN')
        self.assertEqual(cohfs.u32(sb, 504), 1024)
        self.assertEqual(cohfs.u16(sb, 508), 32)
        self.assertEqual(sb[484:490], b'System')
        self.assertEqual(cohfs.check(img, base, strict=True), ([], []))
        sys_ = cohfs.read_tree(img, base)
        self.assertEqual(sys_['Prefs'][long], b'icon' * 300)
        self.assertIn('ClickToFront.info', sys_['C'])
        self.assertEqual(sys_['T'], {'A-directory-name-of-30-bytes!': {}})
        for d in ('C', 'S', 'L', 'Libs', 'Devs'):
            self.assertIn(d, sys_)
        # The pattern follows the system partition; kboot.cfg lists it.
        pat = (base + 1024) * BS
        self.assertEqual(img[pat + BS:pat + BS + 4], (1).to_bytes(4, 'big'))
        cfg = cohfs.read_tree(img, 0)['kboot.cfg'].decode()
        self.assertIn('part 13 %d 1024\n' % base, cfg)
        self.assertIn('part 14 %d 1024\n' % EXEC, cfg)
        self.assertIn('part 0 %d 64\n' % (base + 1024), cfg)

    def test_names_over_30(self):
        f = self.put('f', b'x')
        err, img = self.mkdisk('--lnames', '--put=C/%s=%s' % ('n' * 31, f))
        self.assertIsNone(img)
        self.assertIn('over 30', err)
        _, img = self.mkdisk('--lnames', '--put=C/%s=%s' % ('n' * 30, f))
        self.assertIn('n' * 30, cohfs.read_tree(img, EXEC + 1024)['C'])

    def test_standard_truncates(self):
        f = self.put('f', b'x')
        _, img = self.mkdisk('--put=C/ClickToFront.info=' + f)
        self.assertIn('ClickToFront.i', cohfs.read_tree(img, EXEC)['C'])

    def test_sys_size(self):
        _, img = self.mkdisk('--lnames', '--sys=2048')
        sb = img[(EXEC + 1024 + 1) * BS:]
        self.assertEqual(cohfs.u32(sb, 504), 2048)
        err, img = self.mkdisk('--sys=2048')
        self.assertIsNone(img)
        err, img = self.mkdisk('--lnames', '--sys=10')
        self.assertIn('system partition needs', err)

    def test_sysbuffers(self):
        env = b'E9SYSBUF' + b''.join(v.to_bytes(4, 'big') for v in
                                     (16, 128, 0, 0, 1, 0, 0, 0, 0, 0, 0, 16))
        h = self.put('v7fs-handler', b'code' + env + b'rest')
        _, img = self.mkdisk('--lnames', '--sysbuffers=6', '--module=' + h)
        got = cohfs.read_tree(img, EXEC)['v7fs-handler']
        self.assertEqual(got, b'code' + env[:52] + (6).to_bytes(4, 'big') +
                         b'rest')
        err, img = self.mkdisk('--lnames', '--sysbuffers=3', '--module=' + h)
        self.assertIn('not 4 to 120', err)
        err, img = self.mkdisk('--sysbuffers=6', '--module=' + h)
        self.assertIsNone(img)
        err, img = self.mkdisk('--lnames', '--sysbuffers=6')
        self.assertIn('no single file', err)

    def test_check_finds_damage(self):
        f = self.put('f', b'x')
        _, img = self.mkdisk('--lnames', '--put=C/A-long-command-name=' + f)
        base = EXEC + 1024
        img = bytearray(img)
        # Point the long entry at a free inode.
        root = cohfs.read_tree(img, base)
        self.assertIn('A-long-command-name', root['C'])
        o = img.find(b'A-long-command-name', base * BS)
        img[o - 2:o] = cohfs.p16(70)
        errs, _ = cohfs.check(bytes(img), base)
        self.assertTrue(any('free inode 70' in e for e in errs), errs)


if __name__ == '__main__':
    unittest.main()
