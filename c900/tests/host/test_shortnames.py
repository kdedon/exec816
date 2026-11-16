# SPDX-License-Identifier: MIT
"""Host tests for tools/shortnames.py."""

import io
import os
import random
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                     'tools')
sys.path.insert(0, TOOLS)
import shortnames as sn                                     # noqa: E402

TOOL = os.path.join(TOOLS, 'shortnames.py')


def run(args, stdin=''):
    p = subprocess.run([sys.executable, '-B', TOOL] + args, input=stdin,
                       capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def slurp(path):
    with open(path) as f:
        return f.read()


def defines(text):
    m = {}
    for line in text.splitlines():
        f = line.split()
        if len(f) == 3 and f[0] == '#define' and not f[1].endswith('_H'):
            m[f[1]] = f[2]
    return m


class Assign(unittest.TestCase):
    def valid(self, mapping, names, reserved=()):
        every = set(names) | set(reserved)
        cut = {n[:15] for n in every}
        shorts = list(mapping.values())
        self.assertEqual(len(shorts), len(set(shorts)))
        for lng, s in mapping.items():
            self.assertTrue(sn.is_ident(s), s)
            self.assertLessEqual(len(s), 15)
            self.assertNotIn(s, every)
            self.assertNotIn(s, cut)

    def test_unlockpubscreenlist(self):
        names = ['UnlockPubScreenList', 'UnlockPubScreen', 'LockPubScreen']
        m, _ = sn.assign(names, ())
        self.assertEqual(list(m), ['UnlockPubScreenList'])
        self.assertTrue(m['UnlockPubScreenList'].startswith('UnlockPubS'))
        self.assertNotEqual(m['UnlockPubScreenList'], 'UnlockPubScreen')
        self.valid(m, names)

    def test_deterministic_and_stable(self):
        rnd = random.Random(900)
        base = ['%sLongExternalName%d' % (rnd.choice('ABC'), i)
                for i in range(300)]
        m1, _ = sn.assign(base, ())
        m2, _ = sn.assign(list(reversed(base)), ())
        self.assertEqual(m1, m2)
        more = base + ['BLongExternalNameX%d' % i for i in range(300)]
        m3, _ = sn.assign(more, ())
        for k in base:
            self.assertEqual(m1[k], m3[k])
        self.valid(m3, more)

    def test_salt_on_collision(self):
        lng = 'UnlockPubScreenList'
        first = sn.candidate(lng, 10, 0)
        m, _ = sn.assign([lng], [first])
        self.assertEqual(m[lng], sn.candidate(lng, 10, 1))

    def test_reserved_never_renamed_or_chosen(self):
        m, _ = sn.assign(['OpenWorkBenchObjectA', 'AVeryLongReservedName'],
                         ['AVeryLongReservedName'])
        self.assertNotIn('AVeryLongReservedName', m)
        self.assertIn('OpenWorkBenchObjectA', m)

    def test_collisions_only(self):
        names = ['UnlockPubScreenList', 'UnlockPubScreen',
                 'NoOtherNameLikeThis']
        m, _ = sn.assign(names, (), collisions_only=True)
        self.assertEqual(list(m), ['UnlockPubScreenList'])

    def test_prefix(self):
        m, _ = sn.assign(['UnlockPubScreenList'], (), prefix=12)
        s = m['UnlockPubScreenList']
        self.assertEqual(len(s), 15)
        self.assertTrue(s.startswith('UnlockPubScr'))

    def test_map_kept_and_repaired(self):
        old = {'UnlockPubScreenList': 'UnlockPubSList',
               'OpenWorkBenchObjectA': 'Foo',      # now an input name
               'GoneAwayLongExternal': 'GoneAway1'}
        names = ['UnlockPubScreenList', 'OpenWorkBenchObjectA', 'Foo',
                 'Gone']
        m, notes = sn.assign(names, (), old=old)
        self.assertEqual(m['UnlockPubScreenList'], 'UnlockPubSList')
        self.assertNotEqual(m['OpenWorkBenchObjectA'], 'Foo')
        self.assertEqual(m['GoneAwayLongExternal'], 'GoneAway1')
        self.assertEqual(len(notes), 1)
        m2, _ = sn.assign(names + ['GoneAway1LongName'], (), old=m)
        self.assertNotIn('GoneAway1', [v for k, v in m2.items()
                                       if k != 'GoneAwayLongExternal'])


class Scan(unittest.TestCase):
    SRC = '''
/* comment with fake(); int hidden; */
#define MACRO_NAME_THAT_IS_LONG 1
static int StaticVariableName;
static void StaticFunctionName() { }
typedef struct { int member_one; } TypedefName;
struct Tag { int member_two; long LongStructMemberName; };
struct Tag;
extern struct Library *IntuitionBase;
extern ULONG ExternVariableLongName;
ULONG DefinedVariable = 3, Second[4];
struct Tag Initialised = { 1, 2 };
void (*FunctionPointerVar)();
struct Screen *LockPubScreen();
void UnlockPubScreenList();
LONG AnsiPrototypeName(struct Screen *scr, UBYTE *name);

void
UnlockPubScreen(name, scr)
UBYTE *name;
struct Screen *scr;
{
	int local;
	char *s = "UnlockPubScreen()";
	local = 0;
}

LONG AnsiDefinitionName(struct Screen *scr, UBYTE *name)
{
	return 0;
}
'''

    def test_c(self):
        got = sn.scan_c(self.SRC)
        self.assertEqual(got, {
            'IntuitionBase', 'ExternVariableLongName', 'DefinedVariable',
            'Second', 'Initialised', 'FunctionPointerVar', 'LockPubScreen',
            'UnlockPubScreenList', 'AnsiPrototypeName', 'UnlockPubScreen',
            'AnsiDefinitionName'})

    def test_defines_and_globl(self):
        self.assertEqual(sn.scan_defines(self.SRC),
                         {'MACRO_NAME_THAT_IS_LONG'})
        self.assertEqual(sn.scan_globl('\t.globl\tAsmLongExternalNm_, x\n'),
                         {'AsmLongExternalNm', 'x'})

    def test_crlf(self):
        self.assertEqual(sn.scan_c('int\r\nFoo(a)\r\nint a;\r\n{\r\n}\r\n'),
                         {'Foo'})


class Cli(unittest.TestCase):
    def test_check(self):
        rc, out, _ = run(['--check', '-'],
                         'UnlockPubScreenList\nUnlockPubScreen\n')
        self.assertEqual(rc, 1)
        self.assertIn('collision: 2 names truncate to UnlockPubScreen_', out)
        self.assertIn('warning: UnlockPubScreenList is 19 characters', out)
        rc, out, _ = run(['--check', '-'], 'UnlockPubScreenList\n')
        self.assertEqual(rc, 0)
        rc, out, _ = run(['--check', '--strict', '-'],
                         'UnlockPubScreenList\n')
        self.assertEqual(rc, 1)

    def test_header_scan_reserved_map(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'src')
            os.mkdir(src)
            with open(os.path.join(src, 'a.c'), 'w') as f:
                f.write(Scan.SRC)
            with open(os.path.join(src, 'b.s'), 'w') as f:
                f.write('\t.globl AssemblyDefinedLong_\n')
            res = os.path.join(d, 'reserved.h')
            with open(res, 'w') as f:
                f.write('#define ExecReservedMacroName(x) (x)\n')
            mp = os.path.join(d, 'names.map')
            out = os.path.join(d, 'short.h')
            args = ['--scan', src, '--reserved', res, '--map', mp,
                    '--guard', 'WB_SHORT_H', '-o', out]
            rc, _, err = run(args)
            self.assertEqual(rc, 0, err)
            text = slurp(out)
            self.assertIn('#ifndef WB_SHORT_H', text)
            m = defines(text)
            self.assertEqual(set(m), {'ExternVariableLongName',
                                      'FunctionPointerVar',
                                      'UnlockPubScreenList',
                                      'AnsiDefinitionName',
                                      'AnsiPrototypeName'})
            self.assertEqual(sn.read_map(mp), m)
            rc, _, _ = run(args)
            self.assertEqual(slurp(out), text)

    def test_stdin_list(self):
        rc, out, _ = run(['-'], '# names\nUnlockPubScreenList\n\nFoo\n')
        self.assertEqual(rc, 0)
        m = defines(out)
        self.assertEqual(list(m), ['UnlockPubScreenList'])

    def test_list(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, 'a.c')
            with open(src, 'w') as f:
                f.write(Scan.SRC)
            buf = io.StringIO()
            self.assertEqual(sn.main(['--list', '--scan', src], stdout=buf),
                             0)
            self.assertIn('UnlockPubScreen\n', buf.getvalue())
            self.assertNotIn('StaticVariableName', buf.getvalue())

    def test_bad_name(self):
        rc, _, err = run(['-'], 'not-a-name\n')
        self.assertNotEqual(rc, 0)


if __name__ == '__main__':
    unittest.main()
