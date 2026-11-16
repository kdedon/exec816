#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkdisk.py - build a bootable Exec900 hard-disk image for the C900.

Usage: mkdisk.py --kboot=FILE [--geom=C,H,S,P] [--exec=BLOCKS] [--root=FILE]
                 [--stack=KB] [--rootstack=KB] [--hrmode=MODE]
                 [--lcmode=200|400]
                 [--module=FILE]...
                 [--file=FILE]...
                 [--put=PATH=FILE]... [--mkdir=PATH]...
                 [--lnames [--sys=BLOCKS] [--sysbuffers=N]] OUT KERNEL
       mkdisk.py --floppy OUT

    block   blocks  what
    0       136     boot  COHERENT FS: kboot (as `coherent') + kboot.cfg
    136     1024    exec  COHERENT FS: exec900.sys, bi_part[14], the boot
                    volume Exec900 loads modules and the root program from
    1160    64      bi_part[0]: test pattern (pattern() below)
    1224    64      bi_part[1]: test scratch, zero
    (total 83776 = 704 cyl x 7 heads x 17 spt, the ROM's drive type 3;
    bi_part[15] spans it)

The exec partition is 1024 blocks unless its contents plus headroom (a
quarter of them, at least 128 blocks) need more; then it is that, rounded
up to 64 blocks, and the pattern and scratch regions follow it.
--exec=BLOCKS sets the size instead and fails if the contents do not fit.
The inode area grows past 12 blocks (80 inodes) only when the files need
it, with a quarter spare.

--geom picks another drive type (cylinders, heads, sectors per track,
write precompensation); the image is sized to it and bi_part[15] spans it.
The MiSTer core bench holds at most 21 MB, so its images use 612,4,17,306
(type 1).

--root adds FILE, a hunk program, to the exec partition under its base
name and names it in the partition's `Startup' file: the kernel runs it as
the root Task after the boot modules.

--module adds FILE to the exec partition under its base name and lists it
in the partition's `Modules' file, which the kernel reads at boot; --file
adds FILE without listing it; a FILE given both ways is listed.  Names
are at most 14 bytes.

The exec partition is the AmigaDOS system volume `System' (the
superblock's s_fname): its root also holds the directories C, S, L, Libs
and Devs.  --put stores FILE at PATH (`C/Echo'), making the directories
on the way; as V7 does, a name keeps its first 14 bytes.  --mkdir makes
the directory PATH (`T/Empty') and those on the way, as --put does.

--lnames splits the exec partition in two.  The exec partition, kboot's
slot 14, keeps the kernel, the modules, the root program, Modules,
Startup and the --file files as a standard V7 filesystem, volume `Boot',
so kboot and the kernel read it as before.  The AmigaDOS system volume
`System' (C, S, L, Libs, Devs and the --put and --mkdir paths) moves to a
second partition, slot 13, right after it, in Exec900's long-name V7
variant (cohfs.py): names up to 30 bytes, longer ones refused.  Exec900
lists it as DH1: and SYS:.  --sys=BLOCKS sizes it as --exec does the
exec partition (default: 1024 blocks or as the contents need), and the
pattern and scratch regions follow it.  --sysbuffers=N sets DH1:'s
de_NumBuffers, 4 to 120 cache blocks of 512 bytes (default 16, as DH0:'s),
in the copy of v7fs-handler the disk carries: the long after the 8-byte
tag SYSBUFTAG in its data, 44 bytes into the DosEnvec there.

--stack sets the kernel copy's stack segment size, 32 to 63 KB, or 0 for
the default, 63: the word after the
8-byte tag STACKTAG in the image's data (platform/machine.c kstkcfg).

--rootstack sets the root Task's boot stack size, 1 to 16 KB (default 4):
the word after the 4-byte tag ROOTTAG that follows that word, as the
stack's low offset, 0x10000 less the size.  Boot to the root program
uses about 500 bytes of it, so 1 KB is the minimum.  An image without
ROOTTAG keeps 4 KB and is refused.

--hrmode names the HR card's JP3 jumper, 800 (1024x800 mono), 1024
(1024x1024 mono) or rgbi (640x400 colour), or auto, the default: the
card's strap read, refined by kboot's BF_HR1024.  It is the word after
the 8-byte tag HRTAG in the image's data (devices/console.c khrcfg), 0
for auto, else the CDM_ code (platform/display.h).

--lcmode sets the LC console's text mode: 200 (the default, 80x25 in 8x8
cells at 640x200) or 400 (8x16 cells, 640x400 interlaced).  It is the
height of framebuf.resource's LC mode, the word 2 bytes into the 26-byte
struct FBMode that follows the one holding the 8-byte tag LCTAG
(devices/console.c fbmodes); lc.display draws in that mode.

--floppy writes a 2392-block floppy image holding the pattern.

The boot ROM loads `coherent' from the boot partition; that file is kboot,
which reads kboot.cfg and loads exec900.sys from the exec partition.  The
`part' keyword on the os line asks kboot for its handoff block, which carries
the memory map the kernel builds its heap from.  The image is written sparse.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cohfs                                            # noqa: E402
from sparse import write_sparse                         # noqa: E402

BS = cohfs.BS
GEOM = (704, 7, 17, 352)

BOOT_BASE, BOOT_BLKS, BOOT_ISIZE = 0, 136, 4
EXEC_BASE, EXEC_BLKS, EXEC_ISIZE = 136, 1024, 12
SYSDIRS = ('C', 'S', 'L', 'Libs', 'Devs')
VOLNAME = b'System'

PAT_BLKS = SCR_BLKS = 64
FD_BLKS = 2392
BOOT_SLOT = 14                  # kernel/kernel.h BOOTSLOT
SYS_SLOT = 13                   # dos/v7fs.c SYSSLOT
BOOTNAME = b'Boot'
MODLIST = 'Modules'
STARTUP = 'Startup'

KERNEL = 'exec900.sys'
STACKTAG = b'E9STACKK'
ROOTTAG = b'ROOT'
ROOTKB = (1, 16)
SYSBUFTAG = b'E9SYSBUF'         # dos/v7fs.c sysenv
SYSBUFOFF = 8 + 11 * 4          # de_NumBuffers
SYSBUFS = (4, 120)              # dos/v7fs.c NBUFMIN, NBUFMAX
HRTAG = b'E9HRMODE'
HRMODES = {'auto': 0, '800': 1, '1024': 2, 'rgbi': 3}  # CDM_
LCTAG = b'E9LCMODE'
LCMODE = 26                     # sizeof (struct FBMode)
LCWIDTH = 640
LCMODES = (200, 400)
LOADER = 'coherent'
LABEL = 'Exec900'


def pattern(blk):
    """Block blk of a pattern region: the block number (4 bytes, big
    endian), then byte i = (blk * 31 + i) & 0xff."""
    b = bytearray((blk * 31 + i) & 0xff for i in range(BS))
    b[0:4] = blk.to_bytes(4, 'big')
    return bytes(b)


def kbootcfg(geom, execblks, sysblks=0):
    total = geom[0] * geom[1] * geom[2]
    pat = EXEC_BASE + execblks + sysblks
    return ("\n".join([
        "# kboot.cfg -- generated by c900/tools/mkdisk.py; do not edit.",
        "geom %d %d %d %d" % geom,
        "part 0 %d %d" % (pat, PAT_BLKS),
        "part 1 %d %d" % (pat + PAT_BLKS, SCR_BLKS)] +
        (["part %d %d %d" % (SYS_SLOT, EXEC_BASE + execblks, sysblks)]
         if sysblks else []) + [
        "part %d %d %d" % (BOOT_SLOT, EXEC_BASE, execblks),
        "part 15 0 %d" % total,
        "os %s %d %s part" % (LABEL, EXEC_BASE, KERNEL),
        "console probe",
    ]) + "\n").encode()


def fssize(files, blocks, lnames, opt, what):
    """(blocks, isize) of a partition holding files: blocks, or by
    default 1024 or what the contents need with headroom."""
    blks, inos = cohfs.tree_size(files, lnames)
    inos += 1                                           # BADFIN
    isize = max(EXEC_ISIZE, 2 + (inos + inos // 4 + 7) // 8)
    need = isize + blks
    if blocks is None:
        blocks = max(EXEC_BLKS, -(-(need + max(need // 4, 128)) // 64) * 64)
    elif blocks < need:
        sys.exit("mkdisk: %s=%d: the %s partition needs %d blocks"
                 % (opt, blocks, what, need))
    return blocks, isize


def main():
    kboot = None
    floppy = False
    geom = GEOM
    args = []
    mods, files, puts = [], [], []
    root = None
    stack = None
    rootstack = None
    hrmode = None
    lcmode = None
    execblks = None
    lnames = False
    sysblks = None
    sysbufs = None
    for a in sys.argv[1:]:
        if a == '--floppy':
            floppy = True
        elif a == '--lnames':
            lnames = True
        elif a.startswith('--sys='):
            try:
                sysblks = int(a.split('=', 1)[1])
            except ValueError:
                sys.exit(__doc__)
        elif a.startswith('--sysbuffers='):
            try:
                sysbufs = int(a.split('=', 1)[1])
            except ValueError:
                sys.exit(__doc__)
            if not SYSBUFS[0] <= sysbufs <= SYSBUFS[1]:
                sys.exit("mkdisk: --sysbuffers=%d: not %d to %d"
                         % ((sysbufs,) + SYSBUFS))
        elif a.startswith('--kboot='):
            kboot = a.split('=', 1)[1]
        elif a.startswith('--root='):
            root = a.split('=', 1)[1]
        elif a.startswith('--module='):
            mods.append(a.split('=', 1)[1])
        elif a.startswith('--file='):
            files.append(a.split('=', 1)[1])
        elif a.startswith('--put='):
            path, f = a.split('=', 1)[1].split('=', 1)
            puts.append((path.split('/'), f))
        elif a.startswith('--mkdir='):
            puts.append((a.split('=', 1)[1].split('/'), None))
        elif a.startswith('--stack='):
            try:
                stack = int(a.split('=', 1)[1])
            except ValueError:
                sys.exit(__doc__)
            if stack != 0 and not 32 <= stack <= 63:
                sys.exit("mkdisk: --stack=%d: not 0 or 32 to 63" % stack)
        elif a.startswith('--rootstack='):
            try:
                rootstack = int(a.split('=', 1)[1])
            except ValueError:
                sys.exit(__doc__)
            if not ROOTKB[0] <= rootstack <= ROOTKB[1]:
                sys.exit("mkdisk: --rootstack=%d: not %d to %d"
                         % ((rootstack,) + ROOTKB))
        elif a.startswith('--hrmode='):
            hrmode = HRMODES.get(a.split('=', 1)[1])
            if hrmode is None:
                sys.exit("mkdisk: %s: not auto, 800, 1024 or rgbi" % a)
        elif a.startswith('--lcmode='):
            v = a.split('=', 1)[1]
            if v not in [str(m) for m in LCMODES]:
                sys.exit("mkdisk: %s: not 200 or 400" % a)
            lcmode = int(v)
        elif a.startswith('--exec='):
            try:
                execblks = int(a.split('=', 1)[1])
            except ValueError:
                sys.exit(__doc__)
        elif a.startswith('--geom='):
            try:
                geom = tuple(int(v) for v in a.split('=', 1)[1].split(','))
            except ValueError:
                sys.exit(__doc__)
            if len(geom) != 4:
                sys.exit(__doc__)
        elif a.startswith('-'):
            sys.exit(__doc__)
        else:
            args.append(a)
    if floppy:
        if len(args) != 1:
            sys.exit(__doc__)
        with open(args[0], 'wb') as f:
            f.write(b''.join(pattern(b) for b in range(FD_BLKS)))
        return
    if len(args) != 2 or kboot is None or (
            (sysblks is not None or sysbufs is not None) and not lnames):
        sys.exit(__doc__)
    out, kernel = args
    if not os.path.exists(kboot):
        sys.exit("mkdisk: no kboot at %s (set KBOOT)" % kboot)

    data = open(kernel, 'rb').read()
    if stack is not None or rootstack is not None:
        at = data.find(STACKTAG)
        if at < 0 or data.find(STACKTAG, at + 1) >= 0:
            sys.exit("mkdisk: %s: no single stack tag" % kernel)
        at += len(STACKTAG)
        if stack is not None:
            data = data[:at] + stack.to_bytes(2, 'big') + data[at + 2:]
        at += 2
        if rootstack is not None:
            if data[at:at + len(ROOTTAG)] != ROOTTAG:
                sys.exit("mkdisk: %s: no root stack tag" % kernel)
            at += len(ROOTTAG)
            data = data[:at] + (0x10000 - rootstack * 1024).to_bytes(
                2, 'big') + data[at + 2:]
    if hrmode is not None:
        at = data.find(HRTAG)
        if at < 0 or data.find(HRTAG, at + 1) >= 0:
            sys.exit("mkdisk: %s: no single HR mode tag" % kernel)
        at += len(HRTAG)
        data = data[:at] + hrmode.to_bytes(2, 'big') + data[at + 2:]
    if lcmode is not None:
        at = data.find(LCTAG)
        if at < 0 or data.find(LCTAG, at + 1) >= 0:
            sys.exit("mkdisk: %s: no single LC mode tag" % kernel)
        at += LCMODE
        w, h = (int.from_bytes(data[at + i:at + i + 2], 'big')
                for i in (0, 2))
        if w != LCWIDTH or h not in LCMODES:
            sys.exit("mkdisk: %s: no LC mode after its tag" % kernel)
        data = data[:at + 2] + lcmode.to_bytes(2, 'big') + data[at + 4:]
    execfs = {KERNEL: data}
    for f in mods + [f for f in files if f not in mods] + \
            ([root] if root else []):
        name = os.path.basename(f)
        if len(name) > 14 or name in execfs or name in (MODLIST, STARTUP):
            sys.exit("mkdisk: %s: name too long or repeated" % f)
        execfs[name] = open(f, 'rb').read()
    if sysbufs is not None:
        tagged = [n for n in execfs if execfs[n].count(SYSBUFTAG) == 1]
        if len(tagged) != 1:
            sys.exit("mkdisk: --sysbuffers: no single file with the tag %s"
                     % SYSBUFTAG.decode())
        d = execfs[tagged[0]]
        at = d.find(SYSBUFTAG) + SYSBUFOFF
        execfs[tagged[0]] = d[:at] + sysbufs.to_bytes(4, 'big') + d[at + 4:]
    if mods:
        execfs[MODLIST] = "".join(os.path.basename(f) + "\n"
                                  for f in mods).encode()
    if root:
        execfs[STARTUP] = (os.path.basename(root) + "\n").encode()
    sysfs = {} if lnames else execfs
    for d in SYSDIRS:
        sysfs[d] = {}
    nmax = cohfs.dirent(lnames)[1]
    for path, f in puts:
        if lnames and max(len(p.encode()) for p in path) > nmax:
            sys.exit("mkdisk: %s: a name over %d bytes" % ('/'.join(path),
                                                            nmax))
        d = sysfs
        if f is None:
            for part in path:
                d = d.setdefault(part, {}) if isinstance(d, dict) else None
            if not isinstance(d, dict):
                sys.exit("mkdisk: --mkdir=%s: a file is in the way"
                         % '/'.join(path))
            continue
        for part in path[:-1]:
            d = d.setdefault(part, {})
        name = path[-1][:nmax]
        if not isinstance(d, dict) or name in d:
            sys.exit("mkdisk: --put=%s: repeated" % '/'.join(path))
        d[name] = open(f, 'rb').read()

    execblks, isize = fssize(execfs, execblks, False, '--exec', 'exec')
    if lnames:
        sysblks, sysisize = fssize(sysfs, sysblks, True, '--sys', 'system')
    else:
        sysblks = 0
    total = geom[0] * geom[1] * geom[2]
    pat = EXEC_BASE + execblks + sysblks
    if pat + PAT_BLKS + SCR_BLKS > total:
        sys.exit("mkdisk: a %d-block exec partition does not fit the drive"
                 % (execblks + sysblks))

    img = cohfs.Img(total)
    cfg = kbootcfg(geom, execblks, sysblks)
    used = cohfs.build_fs(img, BOOT_BASE, BOOT_BLKS, BOOT_ISIZE,
                          {LOADER: open(kboot, 'rb').read(), 'kboot.cfg': cfg})
    safe = cohfs.ROM_MIN_HEADS * geom[2]
    if used > safe:
        sys.exit("mkdisk: boot partition uses %d blocks > ROM-safe span %d"
                 % (used, safe))
    if lnames:
        cohfs.build_fs(img, EXEC_BASE, execblks, isize, execfs, BOOTNAME)
        cohfs.build_fs(img, EXEC_BASE + execblks, sysblks, sysisize, sysfs,
                       VOLNAME, lnames=True)
    else:
        cohfs.build_fs(img, EXEC_BASE, execblks, isize, execfs, VOLNAME)
    for b in range(PAT_BLKS):
        img.d[(pat + b) * BS:(pat + b + 1) * BS] = pattern(b)
    write_sparse(out, img.d)
    print("mkdisk: %s: kboot %d B, %s %d B" % (out, os.path.getsize(kboot),
                                              KERNEL, len(data)))


if __name__ == '__main__':
    main()
