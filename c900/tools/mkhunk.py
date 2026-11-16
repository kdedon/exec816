#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""mkhunk.py - convert `ld -r' l.out files into an Amiga hunk file.

Usage: mkhunk.py [--entry=SYM] OUT IN.r...
       mkhunk.py --split [--ld=LD] [--lname] [--keep] [--entry=SYM] OUT INPUT...

Each IN.r is a relocatable Z8001 l.out from `ld-z8001 -r', linked with
platform/progabs.o so that SS and SysBase_ are already resolved.  Each
input's text becomes a code hunk of its own, the first input's hunk 0;
the inputs' shared and private data are packed into data hunks and their
`BSSD' into bss hunks, each section at a long boundary, a new hunk
started when the next section would not fit.  Code hunks come first,
then data, then bss; an empty data or bss hunk is left out.  A global
symbol one input leaves undefined is resolved against another input's
definition.  --entry checks that SYM is at the start of hunk 0, where
LoadSeg's caller enters the program.  One input gives the code, data and
bss hunks of a program under 64 KB.

--split links the inputs itself.  INPUT is an object, an archive or an
object with nothing but absolute symbols (progabs.o).  Objects are taken
in order into groups whose text, data and bss each fit in a hunk; each
group is linked with `LD -r' together with every absolute-only object.
When archives are named, one more group links them with `-u' for every
symbol the object groups leave undefined, so it holds the members they
need.  The groups are then converted as above.  --keep keeps the group
files, OUT.N.r; LD defaults to ld-z8001.  --lname links with `LD -N',
for inputs compiled -VLNAME.

A relocated field is a long segmented pointer: bit 31 (set in an
instruction's long address operand, clear in a C pointer), the segment
in bits 30-24, zero in bits 23-16 and the offset in the low word.  The
file holds bit 31 as linked, zero segment bits and the offset within the
target hunk.  The loader replaces bits 30-16 with the target hunk's
segment and adds its base offset to the low word modulo 65536: a hunk
never crosses a segment, so the sum needs no carry, and a negative
addend wraps as the Z8001's own offset arithmetic does.

Refused: an unresolved external, a global defined twice, a relocation
the hunk format cannot express (byte, word or high-word fields, a
PC-relative reference into another hunk), uninitialised text and a hunk
over 65,520 bytes (with its 8-byte header its allocation stays below
64 KB).

l.out (n.out.h): 16-bit words little endian, 32-bit header values high
word first; segment bytes are big endian.  Relocation records are an
opcode byte, a 4-byte linear address (segment << 16 | offset) and, for a
symbol-based record, a 2-byte symbol number.  An unresolved long field
holds its addend: bit 31 and the low word for an instruction operand
(0x7F in the top byte for a negative addend), the sign-extended addend
for a C pointer.

A symbol name longer than 16 characters (LF_LNAME, 02000 in the header
flags) is in a string table after the relocation: ls_id is zero but for
the name's offset in bytes 4-7, a header-order long.
"""

import os
import struct
import subprocess
import sys

L_SHRI, L_PRVI, L_BSSI, L_SHRD, L_PRVD, L_BSSD, L_DEBUG, L_SYM, L_REL = \
    range(9)
L_ABS, L_REF, L_GLOBAL = 9, 10, 0o20
LR_SEG, LR_PCR, LR_LONG = 0o17, 0o20, 2
L_MAGIC = 0o407
LF_LNAME = 0o2000
HDRSIZE = 48
SYMSIZE = 22

HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32, HUNK_END, HUNK_HEADER = 0x3EC, 0x3F2, 0x3F3
HUNKNAME = {HUNK_CODE: 'code', HUNK_DATA: 'data', HUNK_BSS: 'bss'}
MAXHUNK = 0xFFF0


def die(msg):
    sys.exit("mkhunk: " + msg)


def lw(b, o):
    return struct.unpack_from('<H', b, o)[0]


def ll(b, o):
    return lw(b, o) << 16 | lw(b, o + 2)


def linear(v):
    """A virtual (seg << 24 | offset) address, bit 31 ignored, as
    seg << 16 | offset."""
    return (v >> 8) & 0x7F0000 | v & 0xFFFF


def islout(path):
    with open(path, 'rb') as f:
        b = f.read(2)
    return len(b) == 2 and struct.unpack('<h', b)[0] == L_MAGIC


def readlout(path):
    b = open(path, 'rb').read()
    if len(b) < HDRSIZE or struct.unpack_from('<h', b, 0)[0] != L_MAGIC:
        die("%s: not an l.out" % path)
    size = [ll(b, 8 + 4 * i) for i in range(9)]
    base = linear(ll(b, 44))
    data, off = {}, HDRSIZE
    for i in range(9):
        if i in (L_BSSI, L_BSSD):
            continue
        data[i] = b[off:off + size[i]]
        off += size[i]
    strtab = b[off:] if lw(b, 2) & LF_LNAME else b''
    if size[L_BSSI]:
        die("%s: uninitialised text is not supported" % path)
    # Segment bases as ld lays out a plain (not -n, not -i) image.
    vbase, a = [0] * 6, base
    for i in (L_SHRI, L_PRVI, L_BSSI, L_SHRD, L_PRVD, L_BSSD):
        vbase[i] = a
        a += size[i]
    syms = []
    sb = data[L_SYM]
    for o in range(0, len(sb), SYMSIZE):
        name = sb[o:o + 16].split(b'\0')[0]
        so = ll(sb, o + 4)
        if not name and strtab and 4 <= so < len(strtab):
            name = strtab[so:].split(b'\0')[0]
        name = name.decode('latin-1')
        syms.append((name, struct.unpack_from('<h', sb, o + 16)[0],
                     linear(ll(sb, o + 18))))
    return size, data, vbase, syms


class Section:
    """One input's text, data or bss, placed at off in hunk."""

    def __init__(self, kind, base, body, length):
        self.kind, self.base, self.body, self.length = \
            kind, base, body, length
        self.hunk = self.off = None


class Unit:
    def __init__(self, path):
        self.path = path
        size, data, self.vbase, self.syms = readlout(path)
        self.rel = data[L_REL]
        self.code = Section(HUNK_CODE, self.vbase[L_SHRI],
                            bytearray(data[L_SHRI] + data[L_PRVI]),
                            size[L_SHRI] + size[L_PRVI])
        self.data = Section(HUNK_DATA, self.vbase[L_SHRD],
                            bytearray(data[L_SHRD] + data[L_PRVD]),
                            size[L_SHRD] + size[L_PRVD])
        self.bss = Section(HUNK_BSS, self.vbase[L_BSSD], None, size[L_BSSD])
        self.secof = {L_SHRI: self.code, L_PRVI: self.code,
                      L_SHRD: self.data, L_PRVD: self.data,
                      L_BSSD: self.bss}

    def where(self, addr):
        """The text or data section holding linear addr, and the offset."""
        for s in (self.code, self.data):
            if s.base <= addr < s.base + s.length:
                return s, addr - s.base
        die("%s: relocation at 0x%06x outside text and data"
            % (self.path, addr))


def place(units):
    """Give every section its hunk and offset; return the hunks as
    [type, length, sections]."""
    hunks = []
    for n, u in enumerate(units):
        if n == 0 or u.code.length:
            u.code.hunk, u.code.off = len(hunks), 0
            hunks.append([HUNK_CODE, u.code.length, [u.code]])
    for kind in (HUNK_DATA, HUNK_BSS):
        cur = None
        for u in units:
            s = u.data if kind == HUNK_DATA else u.bss
            if not s.length:
                continue
            at = cur[1] + 3 & ~3 if cur else 0
            if cur is None or at + s.length > MAXHUNK:
                cur, at = [kind, 0, []], 0
                hunks.append(cur)
            s.hunk, s.off = len(hunks) - 1, at
            cur[1] = at + s.length
            cur[2].append(s)
    for n, (t, ln, secs) in enumerate(hunks):
        if (ln + 3) & ~3 > MAXHUNK:
            die("%s: %s hunk %d is %d bytes, over %d"
                % (units[0].path, HUNKNAME[t], n, ln, MAXHUNK))
    return hunks


def globals_(units):
    """Each global definition: name -> (section, offset) or (None, value)."""
    defs, where = {}, {}
    for u in units:
        for name, t, addr in u.syms:
            seg = t & LR_SEG
            if not t & L_GLOBAL or seg == L_REF:
                continue
            if seg == L_ABS:
                d = (None, addr)
            elif seg in u.secof:
                s = u.secof[seg]
                d = (s, addr - s.base)
            else:
                continue
            if name in defs and not (d[0] is None and defs[name] == d):
                die("%s defined in %s and %s (without -VLNAME external "
                    "names keep 15 characters: see tools/symcheck.py)"
                    % (name.rstrip('_'), where[name], u.path))
            defs[name], where[name] = d, u.path
    return defs


def relocate(units, defs):
    """Patch every relocated field; return {hunk: {target: [offsets]}}."""
    relocs = {}
    for u in units:
        r, i = u.rel, 0
        while i < len(r):
            op = r[i]
            addr = lw(r, i + 1) << 16 | lw(r, i + 3)
            i += 5
            seg = op & LR_SEG
            if seg == L_ABS:
                continue                # already applied by ld
            name = None
            if seg == L_SYM:
                name = u.syms[lw(r, i)][0]
                i += 2
                d = defs.get(name)
                if d is None:
                    die("%s: unresolved external %s"
                        % (u.path, name.rstrip('_')))
                if d[0] is None:
                    die("%s: absolute %s is unresolved: link progabs.o "
                        "into every input" % (u.path, name.rstrip('_')))
                t, toff = d
            elif seg in u.secof:
                t, toff = u.secof[seg], None
            else:
                die("%s: relocation at 0x%06x against segment %d"
                    % (u.path, addr, seg))
            s, off = u.where(addr)
            if op & LR_PCR:
                if name is not None or t.hunk != s.hunk:
                    die("%s: PC-relative reference at 0x%06x into another "
                        "hunk" % (u.path, addr))
                continue
            if op >> 5 != LR_LONG:
                die("%s: relocation kind %d at 0x%06x has no hunk form"
                    % (u.path, op >> 5, addr))
            if off & 1:
                die("%s: odd relocation at 0x%06x" % (u.path, addr))
            hi, lo = struct.unpack_from('>HH', s.body, off)
            if name is None:
                bit = hi & 0x8000
                target = ((hi & 0x7F00) << 8 | lo) - t.base + t.off
            else:
                if hi >> 8 in (0x80, 0x7F) and hi & 0xFF == 0:
                    bit = 0x8000
                elif hi in (0x0000, 0xFF00):
                    bit = 0
                else:
                    die("%s: field 0x%04x%04x at 0x%06x for %s"
                        % (u.path, hi, lo, addr, name.rstrip('_')))
                target = t.off + toff + lo
            struct.pack_into('>HH', s.body, off, bit, target & 0xFFFF)
            relocs.setdefault(s.hunk, {}).setdefault(t.hunk, []).append(
                s.off + off)
    return relocs


def checkentry(units, defs, entry):
    for name in (entry, entry + '_'):
        d = defs.get(name)
        if d is None:
            for n, t, addr in units[0].syms:
                if n == name and t & LR_SEG in (L_SHRI, L_PRVI):
                    d = (units[0].code, addr - units[0].code.base)
                    break
        if d is not None:
            break
    if d is None or d[0] is None or d[0].kind != HUNK_CODE or \
            d[0].hunk != 0 or d[0].off + d[1] != 0:
        die("%s: %s is not at the start of the code hunk"
            % (units[0].path, entry))


def convert(out, paths, entry):
    units = [Unit(p) for p in paths]
    hunks = place(units)
    defs = globals_(units)
    relocs = relocate(units, defs)
    if entry is not None:
        checkentry(units, defs, entry)

    o = bytearray()

    def put(*vals):
        for v in vals:
            o.extend(struct.pack('>L', v))

    put(HUNK_HEADER, 0, len(hunks), 0, len(hunks) - 1)
    for t, ln, secs in hunks:
        put((ln + 3) // 4)
    for n, (t, ln, secs) in enumerate(hunks):
        put(t, (ln + 3) // 4)
        if t != HUNK_BSS:
            body = bytearray((ln + 3) & ~3)
            for s in secs:
                body[s.off:s.off + s.length] = s.body
            o.extend(body)
        if n in relocs:
            put(HUNK_RELOC32)
            for tgt in sorted(relocs[n]):
                offs = sorted(relocs[n][tgt])
                put(len(offs), tgt, *offs)
            put(0)
        put(HUNK_END)
    open(out, 'wb').write(o)
    print("mkhunk: %s: %s, %d relocations" % (
        out, ", ".join("%s %d" % (HUNKNAME[t], ln) for t, ln, _ in hunks),
        sum(len(v) for d in relocs.values() for v in d.values())))


def segsizes(path):
    """Text, data and bss bytes of an l.out object."""
    b = open(path, 'rb').read(HDRSIZE)
    size = [ll(b, 8 + 4 * i) for i in range(9)]
    return (size[L_SHRI] + size[L_PRVI] + size[L_BSSI],
            size[L_SHRD] + size[L_PRVD], size[L_BSSD])


LDFLAGS = []


def ldr(ld, out, args):
    cmd = [ld, '-r'] + LDFLAGS + ['-o', out] + args
    if subprocess.call(cmd) != 0:
        die("%s failed" % " ".join(cmd))


def split(out, inputs, ld, keep, entry):
    groups, absobjs, libs = [], [], []
    cur, tot = None, None
    for p in inputs:
        if not islout(p):
            libs.append(p)
            continue
        sz = segsizes(p)
        if not any(sz):
            absobjs.append(p)
            continue
        for what, v in zip(('text', 'data', 'bss'), sz):
            if v > MAXHUNK:
                die("%s: %d bytes of %s, over %d: one object must fit in "
                    "a hunk" % (p, v, what, MAXHUNK))
        if cur is None or any(a + b > MAXHUNK for a, b in zip(tot, sz)):
            cur, tot = [], (0, 0, 0)
            groups.append(cur)
        cur.append(p)
        tot = tuple(a + b for a, b in zip(tot, sz))
    if not groups:
        die("no objects")
    files = []
    for n, g in enumerate(groups):
        files.append("%s.%d.r" % (out, n))
        ldr(ld, files[-1], g + absobjs)
    if libs:
        defined, wanted = set(), []
        for f in files:
            for name, t, addr in readlout(f)[3]:
                if t & L_GLOBAL and t & LR_SEG != L_REF:
                    defined.add(name)
                elif t == L_GLOBAL | L_REF and name not in wanted:
                    wanted.append(name)
        wanted = [w for w in wanted if w not in defined]
        if wanted:
            files.append("%s.%d.r" % (out, len(groups)))
            ldr(ld, files[-1], sum((['-u', w] for w in wanted), []) +
                absobjs + libs)
    print("mkhunk: %s: %d object groups%s" % (
        out, len(groups), ", libraries" if len(files) > len(groups) else ""))
    convert(out, files, entry)
    if not keep:
        for f in files:
            os.remove(f)


def main():
    entry, ld, dosplit, keep = None, 'ld-z8001', False, False
    args = []
    for a in sys.argv[1:]:
        if a.startswith('--entry='):
            entry = a.split('=', 1)[1]
        elif a.startswith('--ld='):
            ld = a.split('=', 1)[1]
        elif a == '--split':
            dosplit = True
        elif a == '--keep':
            keep = True
        elif a == '--lname':
            LDFLAGS.append('-N')
        elif a.startswith('-'):
            sys.exit(__doc__)
        else:
            args.append(a)
    if len(args) < 2:
        sys.exit(__doc__)
    if dosplit:
        split(args[0], args[1:], ld, keep, entry)
    else:
        convert(args[0], args[1:], entry)


if __name__ == '__main__':
    main()
