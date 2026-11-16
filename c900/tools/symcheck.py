#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""symcheck.py - find external names the C900 linker would merge.

Usage: symcheck.py [--strict] [--lname] FILE|DIR...

The MWC tools keep 16 bytes of an external symbol, including the `_' the
compiler appends, and drop the rest without a warning.  Two C names that
agree in their first 15 characters therefore become one symbol.  Object
files already hold the truncated names, so this scans sources instead:
top-level non-static definitions and declarations in .c/.h files and
`.globl' names in .s files.  Directories are searched for those suffixes.

Reports every pair of distinct names that truncate to the same symbol
(exit 1) and warns about each name longer than 15 characters (exit 1 with
--strict).  The scan is a tokenizer, not a C parser; it ignores macros.

--lname checks against the long-name tools instead (cc2 -VLNAME, as -N,
ld -N), which keep 127 characters of a name.
"""

import os
import re
import sys

SIGNIF = 16                       # bytes the linker keeps, `_' included
LNSIGNIF = 127                    # the same under -VLNAME
KEYWORDS = set('''auto break case char const continue default do double else
enum extern float for goto if int long register return short signed sizeof
static struct switch typedef union unsigned void volatile while'''.split())
TOKEN = re.compile(r'[A-Za-z_]\w*|\d\w*|\S')


def strip(text):
    """Remove comments, string/char literals and preprocessor lines."""
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'"(\\.|[^"\\\n])*"', '""', text)
    text = re.sub(r"'(\\.|[^'\\\n])*'", "''", text)
    text = re.sub(r'\\\n', ' ', text)
    return re.sub(r'^[ \t]*#.*$', ' ', text, flags=re.M)


def skip_block(toks, i):
    """toks[i] is `{'; return the index after its matching `}'."""
    depth = 0
    while i < len(toks):
        if toks[i] == '{':
            depth += 1
        elif toks[i] == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def ident(t):
    return re.match(r'[A-Za-z_]\w*$', t) and t not in KEYWORDS


def declarators(stmt):
    """Split a declaration at top-level commas; yield each declarator name."""
    parts, cur, depth = [], [], 0
    for t in stmt:
        if t in '([':
            depth += 1
        elif t in ')]':
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
        for j, t in enumerate(p):
            if t == '(' and j + 1 < len(p) and p[j + 1] == '*':
                inner = [x for x in p[j + 1:] if ident(x)]
                name = inner[0] if inner else None
                break
            if t in '([':
                break
            if ident(t):
                name = t
        if name:
            yield name


def scan_c(text):
    """Return (definitions, references) of top-level external names."""
    toks = TOKEN.findall(strip(text))
    defs, refs = set(), set()
    stmt = []
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == '{':
            # struct/union/enum body or an initializer: part of the statement.
            if '=' in stmt or (stmt and (stmt[-1] in ('struct', 'union', 'enum')
                                         or (len(stmt) > 1 and stmt[-2] in
                                             ('struct', 'union', 'enum')))):
                i = skip_block(toks, i)
                continue
            i = skip_block(toks, i)
            stmt = []
            continue
        if t == '(' and stmt and ident(stmt[-1]) and '=' not in stmt \
                and '(' not in stmt:
            # First parenthesis after a name: a function declarator.  If what
            # follows the `)' is not a declaration delimiter, it's a K&R
            # definition whose parameter declarations run up to `{'.
            name = stmt[-1]
            depth, j = 0, i
            while j < len(toks):
                if toks[j] == '(':
                    depth += 1
                elif toks[j] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            nxt = toks[j + 1] if j + 1 < len(toks) else ';'
            if nxt not in (';', ',', '=', '(', '[', ')'):
                if 'static' not in stmt:
                    defs.add(name)
                while j < len(toks) and toks[j] != '{':
                    j += 1
                i = skip_block(toks, j)
                stmt = []
                continue
        if t == ';':
            # `struct TAG { ... };' declares a tag, not an external name.
            tagonly = len(stmt) == 2 and stmt[0] in ('struct', 'union', 'enum')
            if stmt and stmt[0] != 'typedef' and 'static' not in stmt \
                    and not tagonly:
                extern = 'extern' in stmt
                for name in declarators(stmt):
                    func = False
                    k = stmt.index(name)
                    if k + 1 < len(stmt) and stmt[k + 1] == '(':
                        func = True
                    (refs if extern or func else defs).add(name)
            stmt = []
            i += 1
            continue
        stmt.append(t)
        i += 1
    return defs, refs - defs


def scan_s(text):
    names = set()
    for m in re.finditer(r'^\s*\.globl\s+(.*)$', text, flags=re.M):
        for n in m.group(1).split(','):
            n = n.split(';')[0].strip()
            if n:
                names.add(n)
    return names


def files(args):
    for a in args:
        if os.path.isdir(a):
            for d, _, fs in os.walk(a):
                for f in sorted(fs):
                    if f.endswith(('.c', '.h', '.s')):
                        yield os.path.join(d, f)
        else:
            yield a


def main():
    args = sys.argv[1:]
    global SIGNIF
    strict = '--strict' in args
    if '--lname' in args:
        SIGNIF = LNSIGNIF
    args = [a for a in args if a not in ('--strict', '--lname')]
    if not args or any(a.startswith('-') for a in args):
        sys.exit(__doc__)

    where = {}                    # link name -> {file: kind}
    for path in files(args):
        with open(path, errors='replace') as f:
            text = f.read()
        if path.endswith('.s'):
            for n in scan_s(text):
                where.setdefault(n, {})[path] = 'globl'
            continue
        defs, refs = scan_c(text)
        for n in defs:
            where.setdefault(n + '_', {})[path] = 'def'
        for n in refs:
            where.setdefault(n + '_', {}).setdefault(path, 'ref')

    def show(link):
        c = link[:-1] if link.endswith('_') else link
        return '%s (%s)' % (c, ', '.join('%s %s' % (k, p) for p, k in
                                         sorted(where[link].items())))

    status = 0
    groups = {}
    for link in where:
        groups.setdefault(link[:SIGNIF], []).append(link)
    for key in sorted(groups):
        names = sorted(groups[key])
        if len(names) > 1:
            status = 1
            print('collision: %d names truncate to %s' % (len(names), key))
            for n in names:
                print('  ' + show(n))
    for link in sorted(where):
        if len(link) > SIGNIF:
            if strict:
                status = 1
            print('warning: %s is %d characters; linker keeps %s'
                  % (show(link), len(link) - (link[-1] == '_'),
                     link[:SIGNIF]))
    return status


if __name__ == '__main__':
    sys.exit(main())
