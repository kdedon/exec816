#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""shortnames.py - short link names for C externals the MWC tools would cut.

Usage:
  shortnames.py [options] [NAMES...]     write a #define header
  shortnames.py --check [options] [NAMES...]

The MWC l.out symbol keeps 16 bytes, the `_' the compiler appends
included, so an external keeps 15 significant characters.  The
preprocessor keeps far longer names, so a header of

    #define UnlockPubScreenList UnlockPubS0k3fz

included before every declaration gives each long external a short,
distinct link name.

Inputs (at least one):
  NAMES             files of names, one per line (`-': stdin; `#' starts
                    a comment)
  --scan PATH       C sources and headers (.c, .h; directories searched):
                    top-level non-static function and variable
                    definitions and extern declarations, K&R or ANSI.
                    `.globl' names of .s files are taken as fixed: they
                    are never renamed and no short name may equal them.

Options:
  --reserved PATH   names no short name may equal, and that are never
                    renamed: a name list, or sources/headers/directories
                    (their #define names and externals), e.g. the Exec900
                    include/ tree and generated lvo/ headers.  Repeatable.
  --map FILE        persistent map (`LongName ShortName' per line): its
                    entries are kept while still valid, new ones are
                    added, and the file is rewritten.  Entries for names
                    no longer in the input are kept and stay reserved.
  --prefix N        characters kept from the long name (default 10, 1-14);
                    the rest of the 15 is a base-36 hash.
  --collisions-only rename only long names whose first 15 characters
                    agree with another name's.
  --guard NAME      include guard (default SHORTNAMES_H).
  -o FILE           output (default stdout).
  --check           report names that truncate to one link symbol (exit 1)
                    and warn about each long name (exit 1 with --strict).
  --list            print the names found, one per line, and stop.

Short names are deterministic: a hash of the long name, with a salt
tried in turn only when the hash would collide.  The scan is a
tokenizer, not a C parser: it does not expand macros.
"""

import hashlib
import os
import re
import sys

SIGNIF = 15                     # significant characters of a C external
WIDTH = 15                      # length of a generated short name
DIGITS = '0123456789abcdefghijklmnopqrstuvwxyz'

KEYWORDS = frozenset('''auto break case char const continue default do
double else enum extern float for goto if int long register return short
signed sizeof static struct switch typedef union unsigned void volatile
while'''.split())
IDENT = re.compile(r'[A-Za-z_]\w*$')
TOKEN = re.compile(r'[A-Za-z_]\w*|\d\w*|\S')
DEFINE = re.compile(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)', re.M)


def is_ident(t):
    return IDENT.match(t) is not None and t not in KEYWORDS


# ---- C scanning ------------------------------------------------------

def strip_c(text):
    """Blank comments, literals, continuations and preprocessor lines."""
    text = text.replace('\r\n', '\n')
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'"(\\.|[^"\\\n])*"', '""', text)
    text = re.sub(r"'(\\.|[^'\\\n])*'", "''", text)
    text = re.sub(r'\\\n', ' ', text)
    return re.sub(r'^[ \t]*#.*$', ' ', text, flags=re.M)


def match_close(toks, i):
    """toks[i] opens a bracket; return the index of its partner."""
    pair = {'(': ')', '[': ']', '{': '}'}[toks[i]]
    open_, depth = toks[i], 0
    while i < len(toks):
        if toks[i] == open_:
            depth += 1
        elif toks[i] == pair:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return len(toks) - 1


def declarator_names(stmt):
    """Names declared by a top-level declaration, split at commas."""
    parts, cur, depth = [], [], 0
    for t in stmt:
        if t in '([{':
            depth += 1
        elif t in ')]}':
            depth -= 1
        if t == ',' and depth == 0:
            parts.append(cur)
            cur = []
        else:
            cur.append(t)
    parts.append(cur)
    for p in parts:
        if '=' in p:
            p = p[:p.index('=')]
        name = None
        j = 0
        while j < len(p):
            t = p[j]
            if t == '{':                    # struct body in the type
                j = match_close(p, j) + 1
                continue
            if t == '(' and j + 1 < len(p) and p[j + 1] == '*':
                inner = [x for x in p[j + 1:match_close(p, j)] if is_ident(x)]
                name = inner[0] if inner else None
                break
            if t in '([':
                break
            if is_ident(t):
                name = t
            j += 1
        if name:
            yield name


def scan_c(text):
    """Return the external names of a C file (statics and typedefs left
    out): definitions, extern declarations and function declarations."""
    toks = TOKEN.findall(strip_c(text))
    names = set()
    stmt = []
    i = 0
    n = len(toks)
    while i < n:
        t = toks[i]
        if t == '{':
            # An initializer or a struct/union/enum body continues the
            # statement; any other brace at top level ends it.
            if '=' in stmt or any(x in ('struct', 'union', 'enum')
                                  for x in stmt[-2:]):
                stmt += ['{', '}']
                i = match_close(toks, i) + 1
                continue
            i = match_close(toks, i) + 1
            stmt = []
            continue
        if (t == '(' and stmt and is_ident(stmt[-1]) and '=' not in stmt
                and '(' not in stmt and 'typedef' not in stmt):
            # A function declarator.  What follows its `)' tells a
            # declaration (`;' `,' ...) from a definition, whose K&R
            # parameter declarations run up to the body's `{'.
            close = match_close(toks, i)
            nxt = toks[close + 1] if close + 1 < n else ';'
            if nxt not in (';', ',', '=', '(', '[', ')'):
                if 'static' not in stmt:
                    names.add(stmt[-1])
                j = close + 1
                while j < n and toks[j] != '{':
                    j += 1
                if j < n and toks[j] == '{':
                    j = match_close(toks, j)
                i = j + 1
                stmt = []
                continue
        if t == ';':
            if (stmt and 'typedef' not in stmt and 'static' not in stmt
                    and not (stmt[0] in ('struct', 'union', 'enum') and
                             (len(stmt) == 2 or
                              (stmt[-1] == '}' and '=' not in stmt)))):
                names.update(declarator_names(stmt))
            stmt = []
            i += 1
            continue
        stmt.append(t)
        i += 1
    return names


def scan_defines(text):
    return set(DEFINE.findall(text.replace('\r\n', '\n')))


def scan_globl(text):
    """`.globl' names of an assembly file, as C names where they end
    in the compiler's `_', else as they stand."""
    names = set()
    for m in re.finditer(r'^\s*\.globl\s+(.*)$', text, flags=re.M):
        for s in m.group(1).split(','):
            s = s.split(';')[0].strip()
            if s:
                names.add(s[:-1] if s.endswith('_') else s)
    return names


def source_files(path):
    if os.path.isdir(path):
        for d, subdirs, fs in os.walk(path):
            subdirs.sort()
            for f in sorted(fs):
                if f.endswith(('.c', '.h', '.s')):
                    yield os.path.join(d, f)
    else:
        yield path


def read(path):
    if path == '-':
        return sys.stdin.read()
    with open(path, errors='replace') as f:
        return f.read()


def read_list(path):
    names = []
    for line in read(path).replace('\r\n', '\n').split('\n'):
        line = line.split('#')[0].strip()
        if line:
            names.append(line)
    return names


def is_source(path):
    return os.path.isdir(path) or path.endswith(('.c', '.h', '.s'))


# ---- name assignment -------------------------------------------------

def base36(value, width):
    s = ''
    for _ in range(width):
        value, d = divmod(value, 36)
        s = DIGITS[d] + s
    return s


def candidate(name, prefix, salt):
    key = name if salt == 0 else '%s\0%d' % (name, salt)
    h = int(hashlib.sha1(key.encode()).hexdigest(), 16)
    return name[:prefix] + base36(h, WIDTH - prefix)


def assign(names, reserved, prefix=10, collisions_only=False, old=None):
    """Map each long name to a short one.

    names: the externals to place; reserved: names never renamed and never
    chosen; old: an earlier map, kept where still valid.  Returns
    (mapping, notes): mapping holds every entry, old ones for absent names
    included; notes lists reassignments."""
    old = dict(old or {})
    names = set(names)
    fixed = set(reserved)
    every = names | fixed
    if collisions_only:
        groups = {}
        for nm in every:
            groups.setdefault(nm[:SIGNIF], set()).add(nm)
        long_ = {nm for nm in names - fixed
                 if len(nm) > SIGNIF and len(groups[nm[:SIGNIF]]) > 1}
    else:
        long_ = {nm for nm in names - fixed if len(nm) > SIGNIF}

    # Link names already spoken for: every name as written and as cut.
    taken = set(every) | {nm[:SIGNIF] for nm in every}
    mapping, notes = {}, []

    def free(s):
        return s not in taken and s not in KEYWORDS and is_ident(s) \
            and len(s) <= SIGNIF

    for lng in sorted(old, key=lambda x: (x not in long_, x)):
        s = old[lng]
        if lng in fixed or (lng in names and lng not in long_):
            continue                    # no longer renamed
        if free(s):
            mapping[lng] = s
            taken.add(s)
        elif lng in long_:
            notes.append('%s: map entry %s collides; renamed' % (lng, s))
    for lng in sorted(long_):
        if lng in mapping:
            continue
        salt = 0
        while not free(candidate(lng, prefix, salt)):
            salt += 1
        s = candidate(lng, prefix, salt)
        mapping[lng] = s
        taken.add(s)
    return mapping, notes


def header(mapping, names, guard):
    out = ['/* Generated by shortnames.py: link names for externals over',
           ' * %d characters.  Include before any declaration. */' % SIGNIF,
           '#ifndef %s' % guard, '#define %s' % guard, '']
    width = max([len(n) for n in mapping if n in names] + [0])
    for lng in sorted(n for n in mapping if n in names):
        out.append('#define %-*s %s' % (width, lng, mapping[lng]))
    out += ['', '#endif', '']
    return '\n'.join(out)


def read_map(path):
    m = {}
    if not os.path.exists(path):
        return m
    for k, line in enumerate(read_list(path), 1):
        f = line.split()
        if len(f) != 2 or not is_ident(f[0]) or not is_ident(f[1]):
            sys.exit('%s:%d: expected "LongName ShortName"' % (path, k))
        m[f[0]] = f[1]
    return m


def write_map(path, mapping):
    with open(path, 'w') as f:
        f.write('# shortnames.py map: LongName ShortName.  Keep it under'
                ' version control.\n')
        for lng in sorted(mapping):
            f.write('%s %s\n' % (lng, mapping[lng]))


# ---- check -----------------------------------------------------------

def check(where, strict, out):
    """where: name -> set of origins.  Print collisions and long names."""
    status = 0
    groups = {}
    for nm in where:
        groups.setdefault(nm[:SIGNIF], []).append(nm)

    def show(nm):
        o = sorted(where[nm])
        return '%s (%s)' % (nm, ', '.join(o)) if o else nm

    for key in sorted(groups):
        if len(groups[key]) > 1:
            status = 1
            out.write('collision: %d names truncate to %s_\n'
                      % (len(groups[key]), key))
            for nm in sorted(groups[key]):
                out.write('  %s\n' % show(nm))
    for nm in sorted(where):
        if len(nm) > SIGNIF:
            if strict:
                status = 1
            out.write('warning: %s is %d characters; linker keeps %s_\n'
                      % (show(nm), len(nm), nm[:SIGNIF]))
    return status


# ---- command line ----------------------------------------------------

def parse(argv):
    opts = {'scan': [], 'reserved': [], 'lists': [], 'map': None,
            'prefix': 10, 'collisions_only': False, 'guard': 'SHORTNAMES_H',
            'out': None, 'check': False, 'strict': False, 'list': False}
    takes = {'--scan': 'scan', '--reserved': 'reserved', '--map': 'map',
             '--prefix': 'prefix', '--guard': 'guard', '-o': 'out'}
    flags = {'--check': 'check', '--strict': 'strict', '--list': 'list',
             '--collisions-only': 'collisions_only'}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in takes:
            if i + 1 >= len(argv):
                raise SystemExit('%s needs an argument' % a)
            k = takes[a]
            v = argv[i + 1]
            if isinstance(opts[k], list):
                opts[k].append(v)
            else:
                opts[k] = v
            i += 2
        elif a in flags:
            opts[flags[a]] = True
            i += 1
        elif a in ('-h', '--help'):
            raise SystemExit(__doc__)
        elif a.startswith('-') and a != '-':
            raise SystemExit('unknown option %s\n%s' % (a, __doc__))
        else:
            opts['lists'].append(a)
            i += 1
    try:
        opts['prefix'] = int(opts['prefix'])
    except ValueError:
        raise SystemExit('--prefix wants a number')
    if not 1 <= opts['prefix'] <= WIDTH - 1:
        raise SystemExit('--prefix must be 1-%d' % (WIDTH - 1))
    if not opts['scan'] and not opts['lists']:
        raise SystemExit(__doc__)
    return opts


def main(argv=None, stdout=None):
    stdout = stdout or sys.stdout
    opts = parse(sys.argv[1:] if argv is None else argv)

    where = {}                          # input name -> origins
    fixed = set()                       # .globl names: never renamed
    for path in opts['lists']:
        for nm in read_list(path):
            if not is_ident(nm):
                raise SystemExit('%s: %r is not a C identifier' % (path, nm))
            where.setdefault(nm, set())
    for top in opts['scan']:
        for path in source_files(top):
            text = read(path)
            if path.endswith('.s'):
                for nm in scan_globl(text):
                    where.setdefault(nm, set()).add(path)
                    fixed.add(nm)
            else:
                for nm in scan_c(text):
                    where.setdefault(nm, set()).add(path)

    if opts['list']:
        for nm in sorted(where):
            stdout.write(nm + '\n')
        return 0
    if opts['check']:
        return check(where, opts['strict'], stdout)

    reserved = set(fixed)
    for path in opts['reserved']:
        if is_source(path):
            for f in source_files(path):
                text = read(f)
                if f.endswith('.s'):
                    reserved |= scan_globl(text)
                else:
                    reserved |= scan_defines(text) | scan_c(text)
        else:
            reserved |= set(read_list(path))

    for nm in sorted(set(where) & reserved):
        if len(nm) > SIGNIF:
            sys.stderr.write('shortnames: %s is reserved and over %d '
                             'characters; not renamed\n' % (nm, SIGNIF))

    old = read_map(opts['map']) if opts['map'] else {}
    mapping, notes = assign(where, reserved, opts['prefix'],
                            opts['collisions_only'], old)
    for n in notes:
        sys.stderr.write('shortnames: %s\n' % n)
    text = header(mapping, set(where), opts['guard'])
    if opts['out']:
        with open(opts['out'], 'w') as f:
            f.write(text)
    else:
        stdout.write(text)
    if opts['map']:
        write_map(opts['map'], mapping)
    return 0


if __name__ == '__main__':
    sys.exit(main())
