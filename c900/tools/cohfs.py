#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
# From commodore-900-cpm tools/ at e536cb4; Exec900 adds directories,
# the volume name and check().

"""Write the small COHERENT filesystems read by the ROM and kboot.

Only what the ROM, kboot and Exec900 read is implemented: directories,
regular files, the volume name and the V7 allocation structures.
`cohfs.py --check [--strict] IMAGE [BASE]' checks a filesystem as fsck
would (check()).

Structures per COHERENT's include/sys/filsys.h, fblk.h, ino.h, all values
native Z8001 canonical -- shorts little-endian, longs PDP order (high word
first, each word little-endian):

    block 1     superblock
    block 2..   inodes, 64 bytes each; BADFIN = 1, ROOTIN = 2
                V7 chained free-block stack (NICFREE 64), free-inode cache
                (NICINOD 100), 16-byte directory entries, 13 three-byte
                packed block addresses (10 direct, 1 indirect, 1 double).

Exec900's long-name variant (lnames) differs only in its directory
entries, 32 bytes (a 30-byte name), and in its superblock: s_fsize is 0,
so that COHERENT's mount, df, icheck and dcheck refuse the volume, and
bytes 500-511, past struct filsys, hold LMAGIC, the true s_fsize and the
entry size.

"""

BS = 512
NICFREE = 64
NICINOD = 100
INODE_SIZE = 64
BADFIN, ROOTIN = 1, 2
# A fixed build timestamp, mkfs-style: an image whose inode times came from
# the clock would differ from itself between two builds of the same input.
STAMP = 1784736000

# The ROM starts with drive type 2 (4 heads, 17 sectors) and addresses the
# loader by linear block before kboot installs the medium's true geometry.
# Keep all boot files below 4*17 blocks so both geometries address the same
# cylinder-zero sectors.  This value comes from the ROM dparam table.
ROM_MIN_HEADS = 4

# Blocks of kboot.cfg the loader reads; must match CFGBLK in kboot's
# src/bmain.c.
CFGBLK = 2

# The long-name variant's superblock tail.
LMAGIC = b'E9LN'
S_LMAGIC, S_LFSIZE, S_LDSIZE = 500, 504, 508
LDSIZE = 32


def dirent(lnames):
    """(entry size, name bytes) of a directory entry."""
    return (LDSIZE, LDSIZE - 2) if lnames else (16, 14)


def p16(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def p32(v):
    """PDP order: high word first, each word little-endian."""
    hi, lo = (v >> 16) & 0xFFFF, v & 0xFFFF
    return bytes((hi & 0xFF, hi >> 8, lo & 0xFF, lo >> 8))


def l3put(v):
    """COHERENT l3tol() packing of a block address into three bytes."""
    return bytes(((v >> 16) & 0xFF, v & 0xFF, (v >> 8) & 0xFF))


class FS:
    """One COHERENT filesystem being built inside `img' at block `base'."""

    def __init__(self, img, base, fsize, isize, lnames=False):
        self.img, self.base, self.fsize, self.isize = img, base, fsize, isize
        self.lnames = lnames
        self.ninodes = (isize - 2) * 8
        self.nextino = ROOTIN + 1      # cursor: 1 = BADFIN, 2 = ROOTIN reserved
        self.nextblk = isize           # data block allocation cursor
        self.inodes = {}               # ino -> dict

    def wblk(self, n, data, off=0):
        assert self.isize <= n < self.fsize, "block %d outside data area" % n
        self.img.wblk(self.base + n, data, off)

    def balloc(self):
        if self.nextblk >= self.fsize:
            raise RuntimeError("filesystem full at block %d" % self.nextblk)
        b = self.nextblk
        self.nextblk += 1
        return b

    def ialloc(self):
        if self.nextino > self.ninodes:
            raise RuntimeError("out of inodes")
        i = self.nextino
        self.nextino += 1
        return i

    def write_file(self, data):
        """Store file data; return the 13-entry inode address list."""
        nblk = (len(data) + BS - 1) // BS
        blocks = []
        for i in range(nblk):
            b = self.balloc()
            self.wblk(b, data[i*BS:(i+1)*BS])
            blocks.append(b)
        addrs = [0]*13
        addrs[:min(nblk, 10)] = blocks[:10]
        rest = blocks[10:]
        if rest:
            ind = self.balloc()
            addrs[10] = ind
            ib = bytearray(BS)
            for i, b in enumerate(rest[:128]):
                ib[4*i:4*i+4] = p32(b)
            self.wblk(ind, ib)
            rest = rest[128:]
        if rest:
            dbl = self.balloc()
            addrs[11] = dbl
            db = bytearray(BS)
            for j in range(0, len(rest), 128):
                if j//128 >= 128:
                    raise RuntimeError("file too large (triple indirect)")
                l1 = self.balloc()
                db[4*(j//128):4*(j//128)+4] = p32(l1)
                ib = bytearray(BS)
                for i, b in enumerate(rest[j:j+128]):
                    ib[4*i:4*i+4] = p32(b)
                self.wblk(l1, ib)
            self.wblk(dbl, db)
        return addrs

    def set_inode(self, ino, mode, nlink, uid, gid, size, addrs):
        self.inodes[ino] = dict(mode=mode, nlink=nlink, uid=uid, gid=gid,
                                size=size, addrs=addrs)

    def flush_inodes(self):
        for ino, f in self.inodes.items():
            b = bytearray(INODE_SIZE)
            b[0:2] = p16(f['mode'])
            b[2:4] = p16(f['nlink'])
            b[4:6] = p16(f['uid'])
            b[6:8] = p16(f['gid'])
            b[8:12] = p32(f['size'])
            for i, a in enumerate(f['addrs'][:13]):
                b[12+3*i:15+3*i] = l3put(a)
            b[52:56] = p32(STAMP)   # di_atime (union dia_u @12 is 40 bytes)
            b[56:60] = p32(STAMP)   # di_mtime
            b[60:64] = p32(STAMP)   # di_ctime
            blk = 2 + (ino-1)//8
            self.img.wblk(self.base + blk, bytes(b), ((ino-1) % 8)*INODE_SIZE)

    def flush_super(self, fname=b'noname'):
        # V7 free chain, built by replaying the kernel's bfree() (sys/coh/fs2.c)
        # over every unused data block descending, so allocation later pops
        # ascending.  Chain chunks land in the blocks being freed.
        #
        # The chain ENDS at a block whose df_nfree is 0, not at a full block
        # whose df_free[0] is 0: bfree() spills the in-core stack whenever
        # s_nfree is 0 OR NICFREE, so the very first block freed onto an empty
        # list receives a spill of a zero-deep stack -- an all-zero block.  The
        # walkers stop on that (kernel balloc() `b == 0' test, icheck's
        # `while ((i = fbp->df_nfree) != 0)'), and icheck's own -s rebuild
        # writes the same terminator explicitly.  Seeding nfree=1/free[0]=0
        # instead puts a phantom block 0 in the chain, which icheck counts as
        # free and then flags as a duplicate against the boot block.
        nfree, free = 0, [0]*NICFREE
        tfree = 0
        for b in range(self.fsize - 1, self.nextblk - 1, -1):
            if nfree == 0 or nfree == NICFREE:
                chunk = p16(nfree) + b''.join(p32(x) for x in free)
                self.wblk(b, chunk)
                nfree, free = 0, [0]*NICFREE
            free[nfree] = b
            nfree += 1
            tfree += 1
        sb = bytearray(BS)
        sb[0:2] = p16(self.isize)
        sb[2:6] = p32(self.fsize)
        sb[6:8] = p16(nfree)
        for i, v in enumerate(free):
            sb[8+4*i:12+4*i] = p32(v)
        freeinos = [i for i in range(1, self.ninodes+1)
                    if i not in self.inodes][:NICINOD]
        sb[264:266] = p16(len(freeinos))
        for i, v in enumerate(freeinos):
            sb[266+2*i:268+2*i] = p16(v)
        sb[470:474] = p32(STAMP)                            # s_time
        sb[474:478] = p32(tfree)                            # s_tfree
        sb[478:480] = p16(self.ninodes - len(self.inodes))  # s_tinode
        sb[480:482] = p16(1)                                # s_m
        sb[482:484] = p16(1)                                # s_n
        sb[484:490] = fname[:6].ljust(6, b'\0')             # s_fname
        sb[490:496] = b'nopack'                             # s_fpack
        if self.lnames:
            sb[2:6] = p32(0)
            sb[S_LMAGIC:S_LMAGIC + 4] = LMAGIC
            sb[S_LFSIZE:S_LFSIZE + 4] = p32(self.fsize)
            sb[S_LDSIZE:S_LDSIZE + 2] = p16(LDSIZE)
        self.img.wblk(self.base + 1, bytes(sb))


def build_fs(img, base, fsize, isize, files, fname=b'noname', lnames=False):
    """Write a filesystem of `files' at block `base'.

    `files' maps names to bytes (a regular file) or to another such dict
    (a directory).  Returns the number of blocks used, which the callers
    check against the ROM-safe span.  fname is the superblock's s_fname,
    the volume name Exec900 reports.  lnames builds the long-name variant.
    """
    fs = FS(img, base, fsize, isize, lnames)
    # BADFIN: allocated, empty regular file, nlink 0 -- what the shipped mkfs
    # leaves, and what icheck expects to find.
    fs.set_inode(BADFIN, 0o100000, 0, 0, 0, 0, [0]*13)
    put_dir(fs, ROOTIN, ROOTIN, files)
    fs.flush_inodes()
    fs.flush_super(fname)
    print("fs@%-6d %5d/%d blocks used, %d/%d inodes used"
          % (base, fs.nextblk, fsize, len(fs.inodes), fs.ninodes))
    return fs.nextblk


def file_blocks(n):
    """Blocks write_file() takes for n bytes, indirect blocks included."""
    d = (n + BS - 1) // BS
    return d + (d > 10) + (d > 138) * (1 + (d - 138 + 127) // 128)


def tree_size(files, lnames=False):
    """(data blocks, inodes) that put_dir() takes for `files', the
    directory holding them included."""
    blks, inos = file_blocks(dirent(lnames)[0] * (2 + len(files))), 1
    for data in files.values():
        if isinstance(data, dict):
            b, i = tree_size(data, lnames)
        else:
            b, i = file_blocks(len(data)), 1
        blks, inos = blks + b, inos + i
    return blks, inos


def put_dir(fs, ino, parent, files):
    """Write directory `ino' (in `parent') holding `files'."""
    entries, subdirs = [], 0
    for name in sorted(files):
        data = files[name]
        child = fs.ialloc()
        if isinstance(data, dict):
            put_dir(fs, child, ino, data)
            subdirs += 1
        else:
            addrs = fs.write_file(data)
            fs.set_inode(child, 0o100644, 1, 0, 1, len(data), addrs)
        entries.append((name, child))
    ent = [('.', ino), ('..', parent)] + entries
    nb = dirent(fs.lnames)[1]
    data = b''.join(p16(i) + n.encode()[:nb].ljust(nb, b'\0') for n, i in ent)
    addrs = fs.write_file(data)
    # A directory's link count is one per name pointing at it: its own ".",
    # the ".." of each subdirectory and the entry naming it in its parent.
    # The root has no naming entry in a parent, and dcheck accounts for the
    # absent one anyway (dcheck.c:182), so the root counts it too.
    fs.set_inode(ino, 0o040755, 2 + subdirs + (ino == ROOTIN), 0, 1,
                 len(data), addrs)


class Img:
    """The block-write interface build_fs() writes through."""

    def __init__(self, nblocks):
        self.d = bytearray(nblocks * BS)

    def wblk(self, n, data, off=0):
        assert off + len(data) <= BS
        self.d[n*BS+off : n*BS+off+len(data)] = data


def u16(d, o):
    return d[o] | d[o + 1] << 8


def u32(d, o):
    """PDP order, as p32 writes it."""
    return u16(d, o) << 16 | u16(d, o + 2)


def check(img, base, strict=False):
    """fsck the filesystem at block `base' of the image bytes `img'.

    Returns (errors, warnings), lists of strings.  Errors break the
    filesystem: a block out of range, used twice or both used and free, a
    damaged free list, a directory entry naming a free inode, a reachable
    directory without its "." and "..".  Warnings are what a reset may
    leave and fsck repairs: blocks or inodes neither free nor reachable,
    link counts, the superblock's free counts.  strict makes them errors.
    """
    def blk(n):
        o = (base + n) * BS
        return img[o:o + BS]

    errs, warns = [], []
    sb = blk(1)
    isize, fsize = u16(sb, 0), u32(sb, 2)
    lnames = bytes(sb[S_LMAGIC:S_LMAGIC + 4]) == LMAGIC and \
        u16(sb, S_LDSIZE) == LDSIZE
    if lnames:
        if fsize != 0:
            errs.append("superblock: long names, s_fsize %d not 0" % fsize)
        fsize = u32(sb, S_LFSIZE)
    dsz = dirent(lnames)[0]
    ninodes = (isize - 2) * 8
    if not 2 < isize < fsize or (base + fsize) * BS > len(img):
        return ["superblock: isize %d fsize %d" % (isize, fsize)], []

    # The free list: entry 0 of each part names the block holding the
    # next part (0: none), and is free itself.
    free = set()
    nfree, where = u16(sb, 6), "superblock"
    lst = [u32(sb, 8 + 4 * i) for i in range(NICFREE)]
    while True:
        if nfree > NICFREE:
            errs.append("free list: %s holds %d" % (where, nfree))
            break
        nxt = None
        for i in range(nfree):
            b = lst[i]
            if i == 0 and b == 0:
                break
            if not isize <= b < fsize:
                errs.append("free list: block %d out of range" % b)
            elif b in free:
                errs.append("free list: block %d twice" % b)
            else:
                free.add(b)
                if i == 0:
                    nxt = b
        if nxt is None:
            break
        d = blk(nxt)
        nfree, where = u16(d, 0), "block %d" % nxt
        lst = [u32(d, 2 + 4 * i) for i in range(NICFREE)]

    inodes, owner = {}, {}

    def use(b, ino):
        if not isize <= b < fsize:
            errs.append("inode %d: block %d out of range" % (ino, b))
            return False
        if b in owner:
            errs.append("block %d: inodes %d and %d" % (b, owner[b], ino))
            return False
        if b in free:
            errs.append("block %d: inode %d and free" % (b, ino))
        owner[b] = ino
        return True

    def ind(b, ino, level):
        if use(b, ino):
            d = blk(b)
            for i in range(128):
                v = u32(d, 4 * i)
                if v and level > 1:
                    ind(v, ino, level - 1)
                elif v:
                    use(v, ino)

    for ino in range(1, ninodes + 1):
        d = blk(2 + (ino - 1) // 8)
        o = (ino - 1) % 8 * INODE_SIZE
        mode = u16(d, o)
        if mode == 0:
            continue
        a = [d[o + 12 + 3 * i] << 16 | d[o + 14 + 3 * i] << 8 |
             d[o + 13 + 3 * i] for i in range(13)]
        for i, b in enumerate(a):
            if b and i < 10:
                use(b, ino)
            elif b and i < 12:
                ind(b, ino, i - 9)
            elif b:
                errs.append("inode %d: triple-indirect block" % ino)
        inodes[ino] = dict(mode=mode, nlink=u16(d, o + 2),
                           size=u32(d, o + 8), addr=a)

    def entries(ino):
        """A directory's entries as (inode, name)."""
        f = inodes[ino]
        a, ents = f['addr'], []
        for off in range(0, f['size'], dsz):
            n = off // BS
            b = a[n] if n < 10 else \
                u32(blk(a[10]), 4 * (n - 10)) if n < 138 and a[10] else 0
            if not b:
                errs.append("directory %d: hole at %d" % (ino, off))
                break
            e = blk(b)[off % BS:off % BS + dsz]
            if u16(e, 0):
                ents.append((u16(e, 0), bytes(e[2:dsz]).rstrip(b'\0')))
        return ents

    refs, seen, todo = {ROOTIN: 1}, set(), [ROOTIN]
    while todo:
        ino = todo.pop()
        if ino in seen:
            continue
        seen.add(ino)
        if inodes[ino]['mode'] & 0o170000 != 0o040000:
            continue
        names = {}
        for child, name in entries(ino):
            names[name] = child
            refs[child] = refs.get(child, 0) + 1
            if child not in inodes:
                errs.append("directory %d: %r names free inode %d"
                            % (ino, name, child))
            elif name not in (b'.', b'..'):
                todo.append(child)
        if names.get(b'.') != ino or b'..' not in names:
            errs.append('directory %d: no "." or ".."' % ino)

    for ino, f in sorted(inodes.items()):
        if ino == BADFIN:
            continue
        if ino not in seen:
            warns.append("inode %d: allocated, not reachable" % ino)
        elif refs.get(ino, 0) != f['nlink']:
            warns.append("inode %d: %d links, %d entries"
                         % (ino, f['nlink'], refs.get(ino, 0)))
    lost = [b for b in range(isize, fsize)
            if b not in free and b not in owner]
    if lost:
        warns.append("%d blocks neither free nor used (%d...)"
                     % (len(lost), lost[0]))
    if u32(sb, 474) != len(free):
        warns.append("s_tfree %d, %d free" % (u32(sb, 474), len(free)))
    if u16(sb, 478) != ninodes - len(inodes):
        warns.append("s_tinode %d, %d free"
                     % (u16(sb, 478), ninodes - len(inodes)))
    if strict:
        errs, warns = errs + warns, []
    return errs, warns


def read_tree(img, base):
    """The filesystem at block `base' of the image bytes `img', either
    kind, as build_fs() takes it: names to bytes or to dicts."""
    def blk(n):
        o = (base + n) * BS
        return img[o:o + BS]

    sb = blk(1)
    dsz = LDSIZE if bytes(sb[S_LMAGIC:S_LMAGIC + 4]) == LMAGIC else 16

    def data(ino):
        d = blk(2 + (ino - 1) // 8)
        o = (ino - 1) % 8 * INODE_SIZE
        a = [d[o + 12 + 3 * i] << 16 | d[o + 14 + 3 * i] << 8 |
             d[o + 13 + 3 * i] for i in range(13)]
        size, out = u32(d, o + 8), bytearray()
        for n in range((size + BS - 1) // BS):
            if n < 10:
                b = a[n]
            elif n < 138:
                b = u32(blk(a[10]), 4 * (n - 10))
            else:
                m = n - 138
                b = u32(blk(u32(blk(a[11]), 4 * (m // 128))), 4 * (m % 128))
            out += blk(b) if b else bytes(BS)
        return u16(d, o), bytes(out[:size])

    def tree(ino):
        _, d = data(ino)
        out = {}
        for o in range(0, len(d), dsz):
            child = u16(d, o)
            name = d[o + 2:o + dsz].rstrip(b'\0').decode('latin-1')
            if child and name not in ('.', '..'):
                mode, cd = data(child)
                out[name] = tree(child) if mode & 0o170000 == 0o040000 \
                    else cd
        return out

    return tree(ROOTIN)


def main():
    """cohfs.py --check [--strict] IMAGE [BASE]: check the filesystem at
    block BASE of IMAGE (default 136, the Exec900 boot volume); exit 1 if
    it is damaged, or with --strict not exactly consistent."""
    import sys
    args = sys.argv[1:]
    strict = '--strict' in args
    args = [a for a in args if a != '--strict']
    if not args or args[0] != '--check' or len(args) not in (2, 3):
        sys.exit(main.__doc__)
    with open(args[1], 'rb') as f:
        img = f.read()
    base = int(args[2]) if len(args) == 3 else 136
    errs, warns = check(img, base, strict)
    for w in warns:
        print("cohfs: warning: %s" % w)
    for e in errs:
        print("cohfs: error: %s" % e)
    print("cohfs: %s@%d: %s" % (args[1], base,
                                "damaged" if errs else "consistent"))
    sys.exit(1 if errs else 0)


if __name__ == '__main__':
    main()
