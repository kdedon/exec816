#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Time the phases of tests/perf.c between its marks.

    perf.py emu EMUDIR IMAGE SYS PROG   instructions between marks, C emulator
    perf.py sim LOG.err                 CPU clocks, from simrun -marks output
    perf.py prof SYS PROFILE [N]        simrun -profile samples by function

perf.c is the root program, loaded at boot.  emu first runs IMAGE to the
kernel's krun, whose argument is the program's entry, and adds perfmark's
offset in PROG (the program's `ld -r' output) to it.  It then runs IMAGE
again with a breakpoint on perfmark and reports the instructions retired
between successive marks.  sim reads the "mark N TICK" lines simrun -marks wrote and reports
6 MHz CPU clocks (4 model ticks each).  prof sums simrun's PC samples per
text symbol of SYS and prints the N (default 25) largest shares; samples
outside the kernel text count as "(program)".
"""
import bisect
import os
import re
import subprocess
import sys

NAMES = {
    1: "Forbid/Permit x64",
    2: "Disable/Enable x64",
    3: "Signal self + SetSignal x64",
    4: "Signal/Wait round trip x64",
    5: "PutMsg/GetMsg own port x64",
    6: "PutMsg/ReplyMsg round trip x64",
    7: "AllocMem/FreeMem 32 x64",
    8: "AllocMem CLEAR 200/FreeMem x64",
    9: "CreateIORequest+OpenDevice",
    15: "AllocPooled 32 x64",
    16: "FreePooled 32 x64",
    40: "Obtain/ReleaseSemaphore x64",
    41: "nested Obtain/Release x64",
    42: "ObtainShared/Release x64",
    43: "AttemptSemaphore/Release x64",
    44: "AttemptShared/Release x64",
    10: "DoIO TR_GETSYSTIME x64",
    11: "Sleep(10) idle ticks",
    12: "vt_write 20 lines, null display",
    20: "disk read 1 block x8",
    21: "disk read 8 blocks x8",
    30: "console write screen (no scroll)",
    31: "console write 8 lines, scrolling",
}


def report(marks, unit):
    for (a, ta), (b, tb) in zip(marks, marks[1:]):
        if a in NAMES:
            print("%-36s %12d %s" % (NAMES[a], tb - ta, unit))


def symbols(sysfile):
    tc = os.environ.get("C900_TOOLCHAIN",
                        os.path.expanduser("~/git/commodore-900-toolchain"))
    out = subprocess.run([tc + "/host/build/tools/loutdis", sysfile, "-syms"],
                         capture_output=True, text=True, check=True).stdout
    syms = []
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 3 and p[1] == "SHRI":
            syms.append((int(p[0], 16), p[2]))
    return sorted(syms)


def brkspec(a):
    return "%02X:%04X" % (a >> 24, a & 0xFFFF)


def entry(emudir, image, krun):
    """The root program's entry: krun's argument, as SEG << 24 | OFF."""
    r = subprocess.run([emudir + "/bin/c900", "--firmware=" + emudir + "/rom",
                        "--disk=" + image, "--stop-on=break,port",
                        "--max=400000000", "--break=" + brkspec(krun) + "/1",
                        "--dump=rr14:4+4"],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True)
    m = re.search(r"\[dbg\] mem at=brk.* data=([0-9A-Fa-f]{8})", r.stderr)
    if not m:
        sys.exit("perf.py: the root program was not entered")
    v = int(m.group(1), 16)
    return v & 0x7F00FFFF


def emu(emudir, image, sysfile, prog):
    """Instructions between marks, less those spent in vectored interrupts
    (tvi_ to the interrupt stub's IRET), which are reported apart."""
    tc = os.environ.get("C900_TOOLCHAIN",
                        os.path.expanduser("~/git/commodore-900-toolchain"))
    g = dict((n, a) for a, n in symbols(sysfile))
    p = dict((n, a) for a, n in symbols(prog))
    if "perfmark_" not in p or "start" not in p:
        sys.exit("perf.py: no perfmark_ or start in " + prog)
    g["perfmark_"] = entry(emudir, image, g["krun_"]) + \
        p["perfmark_"] - p["start"]
    iret = None
    for ln in subprocess.run([tc + "/host/build/tools/loutdis", sysfile, "-v"],
                             capture_output=True, text=True).stdout.splitlines():
        m = re.match(r"\s+([0-9a-f]{4}): IRET", ln)
        if m and int(m.group(1), 16) > (g["intr"] & 0xFFFF):
            iret = (g["intr"] & 0xFF000000) | int(m.group(1), 16)
            break
    pm, tv, ir = (brkspec(g["perfmark_"]), brkspec(g["tvi_"]), brkspec(iret))
    r = subprocess.run([emudir + "/bin/c900", "--firmware=" + emudir + "/rom",
                        "--disk=" + image, "--stop-on=port", "--max=400000000",
                        "--break=" + pm + "/0", "--break=" + tv + "/0",
                        "--break=" + ir + "/0", "--dump=rr14:4+2"],
                       stdin=subprocess.DEVNULL, capture_output=True, text=True)
    marks, at, pc = [], None, None
    intr, ints, enter = 0, [], None
    for ln in r.stderr.splitlines():
        b = re.search(r"\[dbg\] brk pc=(\S+) .* insns=(\d+)", ln)
        if b:
            pc, at = b.group(1), int(b.group(2))
            if pc == tv:
                enter = at
            elif pc == ir and enter is not None:
                intr += at - enter
                ints.append(at - enter)
                enter = None
            continue
        d = re.search(r"\[dbg\] mem at=brk.* data=([0-9A-Fa-f]{4})", ln)
        if d and pc == pm:
            marks.append((int(d.group(1), 16), at - intr))
    report(marks, "insns")
    if ints:
        ints.sort()
        print("%-36s %12d insns (median of %d; min %d, max %d)" %
              ("interrupt entry to exit", ints[len(ints) // 2], len(ints),
               ints[0], ints[-1]))


def sim(errlog):
    marks = []
    with open(errlog) as f:
        for ln in f:
            p = ln.split()
            if len(p) == 3 and p[0] == "mark":
                marks.append((int(p[1]), int(p[2]) // 4))
    report(marks, "clocks")


CFUNC = re.compile(r"^([A-Za-z_]\w*)\(([\w, ]*)\)\s*$")


def cfuncs(src):
    """Function names of a K&R source in definition order."""
    names = []
    with open(src, errors="replace") as f:
        for ln in f:
            m = CFUNC.match(ln)
            if m:
                names.append(m.group(1))
    return names


def funcstarts(dis, path):
    """Offsets of the functions in a disassembly: code splits after each
    unconditional RET."""
    starts, new = [], True
    for ln in subprocess.run([dis, path, "-v"], capture_output=True,
                             text=True).stdout.splitlines():
        m = re.match(r"\s+([0-9a-f]{4,8}): (\S+)\s*(\S*)", ln)
        if not m:
            continue
        if new:
            starts.append(int(m.group(1), 16))
        new = m.group(2) == "RET" and m.group(3) == "T"
    return starts


def objfuncs(sysfile):
    """Every function of the image as (address, name).  The linker shortens
    code, so an object's functions are matched to the image's by order,
    anchored at a global symbol; statics are named from the C source."""
    tc = os.environ.get("C900_TOOLCHAIN",
                        os.path.expanduser("~/git/commodore-900-toolchain"))
    dis = tc + "/host/build/tools/loutdis"
    c900 = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    gsyms = symbols(sysfile)
    glob = dict((n, a) for a, n in gsyms)
    seg = gsyms[0][0] & 0xFF000000 if gsyms else 0
    sysf = [seg | o for o in funcstarts(dis, sysfile)]
    index = dict((a, i) for i, a in enumerate(sysf))
    names = {}
    objdir = os.path.join(os.path.dirname(sysfile), "obj")
    for o in sorted(os.listdir(objdir)):
        if not o.endswith(".o"):
            continue
        p = os.path.join(objdir, o)
        anchor = None
        for ln in subprocess.run([dis, p, "-syms"], capture_output=True,
                                 text=True).stdout.splitlines():
            q = ln.split()
            if len(q) == 3 and q[1] == "SHRI" and glob.get(q[2]) in index:
                anchor = (int(q[0], 16), index[glob[q[2]]])
                break
        if anchor is None:
            continue
        if o.startswith("t_"):
            stem, dirs = o[2:-2], ("tests",)
        else:
            stem, dirs = o[:-2], ("kernel", "devices", "platform")
        src = [os.path.join(c900, d, stem + ".c") for d in dirs]
        src = [f for f in src if os.path.exists(f)]
        cn = cfuncs(src[0]) if src else []
        ostarts = funcstarts(dis, p)
        if anchor[0] not in ostarts:
            continue
        base = anchor[1] - ostarts.index(anchor[0])
        for k in range(len(ostarts)):
            if 0 <= base + k < len(sysf):
                nm = cn[k] if len(cn) == len(ostarts) else "+%d" % k
                names[sysf[base + k]] = stem + ":" + nm
    for a, n in gsyms:
        names.setdefault(a, n)
    for a in sysf:
        names.setdefault(a, "?%08x" % a)
    return sorted(names.items())


def prof(sysfile, path, top=25):
    syms = objfuncs(sysfile)
    addrs = [a for a, _ in syms]
    per, total = {}, 0
    with open(path) as f:
        for ln in f:
            pc, n = ln.split()
            pc, n = int(pc, 16), int(n)
            # The model's PC is SEG<<16 | OFF; symbols are SEG<<24 | OFF.
            a = (pc >> 16) << 24 | (pc & 0xFFFF)
            i = bisect.bisect_right(addrs, a) - 1
            if a & 0xFF000000 != addrs[0] & 0xFF000000:
                name = "(program)"
            else:
                name = syms[i][1] if i >= 0 else "?"
            per[name] = per.get(name, 0) + n
            total += n
    for name, n in sorted(per.items(), key=lambda x: -x[1])[:int(top)]:
        print("%-20s %6.2f%% %8d" % (name, 100.0 * n / total, n))
    print("%-20s %7s %8d" % ("total", "", total))


if __name__ == "__main__":
    if len(sys.argv) == 6 and sys.argv[1] == "emu":
        emu(*sys.argv[2:])
    elif len(sys.argv) == 3 and sys.argv[1] == "sim":
        sim(sys.argv[2])
    elif len(sys.argv) in (4, 5) and sys.argv[1] == "prof":
        prof(*sys.argv[2:])
    else:
        sys.exit(__doc__)
