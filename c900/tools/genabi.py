#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""genabi.py - generate C900 C headers from the upstream abi/*.json files.

Usage: genabi.py ABIDIR OUTDIR

Writes OUTDIR/exec/abi/{nodes,tasks,ports,memory,io,errors}.h, the
generated parts of the Amiga <exec/...> headers of the same names, which
include them, and OUTDIR/exec/abi.h, which includes those Amiga headers.
A fragment has no includes: its Amiga header includes the prerequisites.
Constants and record field ORDER come from the
upstream JSON; C900 types and layout come from the MWC compiler (32-bit
segmented pointers, word alignment), so upstream byte offsets are ignored.
EXTRA adds the Amiga fields the C900 build needs and upstream lacks.
timer-device.json is not used: its command numbers and records clash with
AmigaOS's, which <devices/timer.h> defines instead.
An Action! field type with no mapping below stops the build: a new upstream
type needs a deliberate C900 decision, not a guess.
"""

import json
import os
import sys

SCALARS = {
    'BYTE': 'UBYTE',
    'CHAR': 'UBYTE',
    'CARD': 'UWORD',
    'INT': 'WORD',
    'LONGCARD': 'ULONG',
    'LONGINT': 'LONG',
    'ADDRESS': 'APTR',
    'BYTE POINTER': 'UBYTE *',
    'EXECLISTS.Node': 'struct Node',
    'EXECLISTS.List': 'struct List',
}

# Generated records, in dependency order: (json file, json path, C name,
# Amiga header).
RECORDS = [
    ('tasks.json', ('task',), 'Task', 'tasks'),
    ('ports.json', ('records', 'MsgPort'), 'MsgPort', 'ports'),
    ('ports.json', ('records', 'Message'), 'Message', 'ports'),
    ('heap-v1.json', ('records', 'MemChunk'), 'MemChunk', 'memory'),
    ('heap-v1.json', ('records', 'MemHeader'), 'MemHeader', 'memory'),
    ('io.json', ('records', 'IORequest'), 'IORequest', 'io'),
    ('io.json', ('records', 'IOStdReq'), 'IOStdReq', 'io'),
]

# The fragments, in the order <exec/abi.h> includes their Amiga headers.
HEADERS = ['nodes', 'tasks', 'ports', 'memory', 'io', 'errors']

# Signed fields: Action! has no signed BYTE, the classic API does.
SIGNED = {('Task', 'tc_IDNestCnt'), ('Task', 'tc_TDNestCnt'),
          ('IORequest', 'io_Error'), ('IOStdReq', 'io_Error')}

# Fields that exist only for 65816 layout.  mc_Pad aligns a 3-byte pointer;
# with 4-byte pointers it would push MemChunk past the 8-byte granule.
DROP = {('MemChunk', 'mc_Pad')}

# C900 fields upstream lacks, inserted after the named upstream field in
# Amiga Exec's order: (record, after) -> [(C type, name)].
EXTRA = {('Task', 'tc_SigExcept'): [('APTR', 'tc_TrapData'),
                                    ('APTR', 'tc_TrapCode')]}

RECORD_NAMES = {r[2] for r in RECORDS} | {'Task', 'MsgPort', 'Message',
                                          'MemChunk', 'IORequest'}

# Upstream types the binding as an untyped byte pointer; C900 drivers are
# struct Device and struct Unit records (include/exec/devices.h).
FIELDTYPE = {('IORequest', 'io_Device'): 'struct Device *',
             ('IORequest', 'io_Unit'): 'struct Unit *',
             ('IOStdReq', 'io_Device'): 'struct Device *',
             ('IOStdReq', 'io_Unit'): 'struct Unit *'}


def ctype(rec, name, t):
    if (rec, name) in FIELDTYPE:
        return FIELDTYPE[(rec, name)]
    if (rec, name) in SIGNED and t == 'BYTE':
        return 'BYTE'
    if t in SCALARS:
        return SCALARS[t]
    if t.endswith(' POINTER') and t[:-8] in RECORD_NAMES:
        return 'struct %s *' % t[:-8]
    if t in RECORD_NAMES:
        return 'struct %s' % t
    sys.exit("genabi: %s.%s has Action! type %r with no C900 mapping"
             % (rec, name, t))


def load(abidir, name):
    with open(os.path.join(abidir, name)) as f:
        return json.load(f)


def dig(d, path):
    for p in path:
        d = d[p]
    return d


def consts(out, prefix, table, rename=None):
    for k, v in table.items():
        n = (rename or {}).get(k, k)
        if isinstance(v, int) and v < 0:
            out.append('#define %s%s (%d)' % (prefix, n, v))
        elif isinstance(v, int) and v > 0x7fff:
            out.append('#define %s%s 0x%XL' % (prefix, n, v))
        else:
            out.append('#define %s%s %d' % (prefix, n, v))


def write(outdir, rel, lines):
    path = os.path.join(outdir, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = '\n'.join(lines) + '\n'
    if not os.path.exists(path) or open(path).read() != text:
        with open(path, 'w') as f:
            f.write(text)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    abidir, outdir = sys.argv[1:]
    tasks = load(abidir, 'tasks.json')
    ports = load(abidir, 'ports.json')
    heap = load(abidir, 'heap-v1.json')
    io = load(abidir, 'io.json')

    h = {n: [] for n in HEADERS}
    pc = ports['constants']
    h['nodes'].append('#define NT_TASK %d' % tasks['node_type'])
    h['nodes'].append('#define NT_MEMORY %d' % heap['node_type'])
    consts(h['nodes'], '', {k: v for k, v in pc.items() if k.startswith('NT_')})
    h['tasks'].append('#define EXEC_TASKS_VERSION 0x%04X' % tasks['version'])
    consts(h['tasks'], 'TS_', tasks['states'])
    h['tasks'].append('#define SIGB_USER_FIRST %d'
                      % tasks['constants']['USER_SIGNAL_FIRST'])
    h['tasks'].append('#define SIG_COUNT %d'
                      % tasks['constants']['SIGNAL_COUNT'])
    consts(h['ports'], '',
           {k: v for k, v in pc.items() if not k.startswith('NT_')})
    consts(h['memory'], 'MEMF_', heap['flags'], {'24BITDMA': 'DMA24'})
    h['memory'].append('#define MEM_BLOCKSIZE %d' % heap['alignment'])
    h['io'] += ['struct Device;', 'struct Unit;']
    consts(h['io'], '', io['constants'])
    consts(h['errors'], '', io['io_errors'])
    for n in HEADERS:
        h[n].append('')

    placed = set()
    for fname, path, cname, hdr in RECORDS:
        rec = dig(load(abidir, fname), path)
        o = h[hdr]
        o.append('struct %s {' % cname)
        for fl in rec['fields']:
            name, t = fl[0], fl[1]
            if (cname, name) in DROP:
                continue
            ct = ctype(cname, name, t)
            sep = '' if ct.endswith('*') else ' '
            o.append('\t%s%s%s;' % (ct, sep, name))
            for et, en in EXTRA.get((cname, name), ()):
                o.append('\t%s %s;' % (et, en))
                placed.add((cname, name))
        o.append('};')
        o.append('')
    for k in set(EXTRA) - placed:
        sys.exit("genabi: upstream %s has no field %s to follow" % k)

    gen = ('/* Generated by c900/tools/genabi.py from the upstream abi JSON'
           ' files; do not edit. */')
    for n in HEADERS:
        guard = 'EXEC_ABI_%s_H' % n.upper()
        write(outdir, os.path.join('exec', 'abi', n + '.h'),
              [gen, '/* Part of <exec/%s.h>, which includes it. */' % n,
               '#ifndef ' + guard, '#define ' + guard, ''] + h[n]
              + ['#endif'])
    write(outdir, os.path.join('exec', 'abi.h'),
          [gen, '#ifndef EXEC_ABI_H', '#define EXEC_ABI_H', '']
          + ['#include <exec/%s.h>' % n for n in HEADERS] + ['', '#endif'])


if __name__ == '__main__':
    main()
