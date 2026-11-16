#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""drift.py - report upstream Exec816 changes the C900 port has not reviewed.

Usage: drift.py [--ref REF] [--map FILE] [--check] [-v]
       drift.py --mark UNIT... [--to REF] [--map FILE]

Reads c900/UPSTREAM.json.  For each unit, lists the commits in
<reviewed>..<ref> that touch the unit's upstream paths.  REF defaults to
upstream/main when that remote branch exists, otherwise HEAD.

  --check      exit 1 if a ported unit has unreviewed commits or a tracked
               path is missing at REF (for CI).  Not-ported areas never fail.
  --mark UNIT  set UNIT's reviewed hash to --to REF (default: the chosen
               ref) after the port has been brought in line.  UNIT may be a
               ported unit, a not-ported area or `all'.
  -v           also list changed upstream files that no entry covers.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
C900 = os.path.dirname(HERE)
ROOT = os.path.dirname(C900)
DEFAULT_MAP = os.path.join(C900, 'UPSTREAM.json')


def git(*args, check=True):
    p = subprocess.run(['git', '-C', ROOT] + list(args),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       universal_newlines=True)
    if check and p.returncode != 0:
        sys.exit('drift: git %s: %s' % (' '.join(args), p.stderr.strip()))
    return p


def resolve(ref):
    p = git('rev-parse', '--verify', '-q', ref + '^{commit}', check=False)
    return p.stdout.strip() if p.returncode == 0 else None


def default_ref(upstream):
    cand = '%s/%s' % (upstream.get('remote', 'upstream'),
                      upstream.get('branch', 'main'))
    if resolve('refs/remotes/' + cand):
        return cand
    return 'HEAD'


def path_exists(ref, path):
    return git('cat-file', '-e', '%s:%s' % (ref, path),
               check=False).returncode == 0


def commits(base, ref, paths):
    out = git('log', '--oneline', '%s..%s' % (base, ref), '--', *paths).stdout
    return [l for l in out.splitlines() if l]


def covered(path, prefixes):
    for p in prefixes:
        if path == p or path.startswith(p.rstrip('/') + '/'):
            return True
    return False


def report(data, ref, verbose):
    sha = resolve(ref)
    if not sha:
        sys.exit('drift: unknown ref %s' % ref)
    print('upstream ref: %s (%s)' % (ref, sha[:12]))
    stale = False
    groups = [('units', 'port'), ('not_ported', 'not ported')]
    allpaths = []
    bases = set()
    for key, label in groups:
        for name, unit in data.get(key, {}).items():
            base = unit['reviewed']
            paths = unit['upstream']
            allpaths += paths
            if not resolve(base):
                print('%s: reviewed commit %s not in this repository '
                      '(fetch upstream?)' % (name, base[:12]))
                stale = stale or key == 'units'
                continue
            bases.add(base)
            missing = [p for p in paths if not path_exists(ref, p)]
            log = commits(base, ref, paths)
            if key == 'not_ported':
                if log or missing:
                    print('%s: not ported; %d upstream commit(s) since %s'
                          % (name, len(log), base[:12]))
                for p in missing:
                    print('  warning: %s no longer exists at %s' % (p, ref))
                continue
            if log or missing:
                stale = True
                print('%s: %d upstream commit(s) to review since %s'
                      % (name, len(log), base[:12]))
                for l in log:
                    print('  ' + l)
                for p in missing:
                    print('  warning: %s no longer exists at %s '
                          '(renamed or deleted?)' % (p, ref))
            else:
                print('%s: up to date' % name)
    if bases:
        uncovered = set()
        for base in bases:
            out = git('diff', '--name-only', base, ref, '--', '.',
                      ':(exclude)c900').stdout
            uncovered.update(f for f in out.splitlines()
                             if f and not covered(f, allpaths))
        if uncovered:
            print('other: %d changed upstream file(s) outside any entry%s'
                  % (len(uncovered), '' if verbose else ' (-v lists them)'))
            if verbose:
                for f in sorted(uncovered):
                    print('  ' + f)
    return stale


def mark(data, path, names, to):
    sha = resolve(to)
    if not sha:
        sys.exit('drift: unknown ref %s' % to)
    every = {}
    for key in ('units', 'not_ported'):
        for name, unit in data.get(key, {}).items():
            every[name] = unit
    if names == ['all']:
        names = list(every)
    for name in names:
        if name not in every:
            sys.exit('drift: no unit %s (have: %s)'
                     % (name, ', '.join(sorted(every))))
    for name in names:
        old = every[name]['reviewed']
        every[name]['reviewed'] = sha
        print('%s: %s -> %s' % (name, old[:12], sha[:12]))
    tmp = path + '.tmp'
    with open(tmp, 'w') as f:
        json.dump(data, f, indent=2)
        f.write('\n')
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser(
        description='Report upstream changes the C900 port has not reviewed.')
    ap.add_argument('--ref', help='upstream ref (default upstream/main, '
                    'else HEAD)')
    ap.add_argument('--map', default=DEFAULT_MAP, help='UPSTREAM.json path')
    ap.add_argument('--check', action='store_true',
                    help='exit 1 if any ported unit is stale')
    ap.add_argument('--mark', nargs='+', metavar='UNIT',
                    help="set reviewed hash for UNIT(s) or `all'")
    ap.add_argument('--to', help='ref for --mark (default: --ref)')
    ap.add_argument('-v', '--verbose', action='store_true',
                    help='list changed files outside every entry')
    a = ap.parse_args()

    with open(a.map) as f:
        data = json.load(f)
    ref = a.ref or default_ref(data.get('upstream', {}))
    if a.mark:
        mark(data, a.map, a.mark, a.to or ref)
        return 0
    if a.to:
        ap.error('--to needs --mark')
    stale = report(data, ref, a.verbose)
    return 1 if stale and a.check else 0


if __name__ == '__main__':
    sys.exit(main())
