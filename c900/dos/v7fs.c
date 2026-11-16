/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The boot volume's filesystem handler: COHERENT's V7 filesystem
 * (tools/cohfs.py) on wd.device, read and write, as an AmigaDOS packet
 * handler.  A boot module: its romtag lists the device DH0 on the boot
 * slot and, when the disk has a system slot, DH1 on it as SYS:; DOS
 * starts a handler process on a device's first use, both on this
 * module's seglist.  A handler then lists its volume, named from the
 * superblock's s_fname.  A process keeps its volume's state in a struct
 * v7 it allocates, which every function that needs it is passed, so
 * the code holds no per-volume data.
 *
 * On disk, shorts are little endian and longs PDP order (high word
 * first); a block address in an inode is three bytes, high, low,
 * middle.  Inode n is 64 bytes in block 2 + (n - 1) / 8; the root is
 * inode 2.  Files reach through the double-indirect block (8.4 MB).
 * A directory entry is a 2-byte inode and a 14-byte name.  Names keep
 * their first 14 bytes, as V7's namei does, so S:Startup-Sequence is
 * S/Startup-Sequen; they are looked up ignoring ASCII case and stored as
 * given.  Exec900's long-name variant, marked in the superblock, has
 * 32-byte entries with 30-byte names; a longer name is refused, as FFS
 * refuses one, and its s_fsize is 0 so that COHERENT's tools reject it.
 *
 * Free blocks are COHERENT's chained stack (s_free, NICFREE 64), free
 * inodes its cache (s_inode, refilled by scanning).
 *
 * Blocks go through a cache of de_NumBuffers buffers with delayed
 * write-back (bsync).  Changes reach the disk in an order that leaves a
 * consistent filesystem after a reset at any point: file data, the
 * superblock, indirect blocks, inodes, directory entries.  A new
 * directory block is cleared on the disk before anything points to it;
 * removed references reach the disk before their blocks and inodes are
 * freed; a chain block of the free list is written before the superblock
 * that names it.
 *
 * A lock's fl_Key is its inode, with its directory's inode in the high
 * word (a file's name is found there); fl_Link chains every lock.  A
 * file handle's fh_Args is a struct vfile.  fib_DiskKey is the byte
 * offset of the next directory entry.  An exclusive lock or a
 * MODE_NEWFILE handle excludes every other use of the object, and an
 * object in use cannot be deleted.
 *
 * Protection is the owner's mode bits: 0400 is a clear R bit, 0200
 * clear W and D bits, 0100 a clear E bit; SetProtection sets those
 * three and keeps the group and other bits, and nothing enforces them.
 * A date is the inode's modification time, which SetFileDate sets.
 */
#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <devices/wd.h>
#include <devices/timer.h>
#include <lvo/dos_lib.h>

struct DosLibrary *DOSBase;

#define BLK	512
#define NBUFDEF	16		/* de_NumBuffers by default */
#define NBUFMIN	4
#define NBUFMAX	120		/* the cache stays inside a segment */
#define RAMAX	8		/* blocks per read-ahead */
#define IDLEDLY	100		/* ticks without a packet before a flush */
#define MAXDLY	500		/* ticks a change waits at most */
#define ROOTINO	2
#define ROOTKEY	((ULONG)ROOTINO << 16 | ROOTINO)
#define BOOTSLOT 14		/* kboot's slot for the boot volume */
#define IFMT	0170000
#define IFDIR	0040000
#define IFREG	0100000
#define NAMEMAX	30		/* the longest name of either kind */
#define V7NAME	14
#define SYSSLOT	13		/* the long-name system volume's slot */
#define NICFREE	64
#define NICINOD	100
#define V7EPOCH	252460800L	/* 1978-01-01 in V7 seconds since 1970 */

/* Superblock offsets */
#define S_ISIZE	0
#define S_FSIZE	2
#define S_NFREE	6
#define S_FREE	8
#define S_NINODE 264
#define S_INODE	266
#define S_TIME	470
#define S_TFREE	474
#define S_TINODE 478
#define S_FNAME	484
#define S_LMAGIC 500		/* the long-name variant: "E9LN", */
#define S_LFSIZE 504		/* its s_fsize, */
#define S_LDSIZE 508		/* and its entry size, 32 */

struct vino {
	UWORD i_Mode;
	UWORD i_Nlink;
	ULONG i_Size;
	ULONG i_Addr[13];
	ULONG i_Mtime;
};

struct vfile {
	struct vfile *f_Next;
	ULONG f_Key;
	ULONG f_Pos;
	int f_Excl;
};

/* A cache buffer: its block (0: none) and whether it waits to be
 * written, as the class bsync writes it in. */
struct cslot {
	ULONG c_Blk;
	UBYTE c_Class;
	UBYTE c_Ref;			/* used since the hand passed */
};

#define C_CLEAN	0
#define C_DATA	1			/* file data */
#define C_IND1	2			/* indirect block of data blocks */
#define C_IND2	3			/* double-indirect block */
#define C_INODE	4
#define C_DIR	5			/* directory entries */

/* A volume's state.  Each handler process has its own, made when it
 * starts, so that one loaded handler serves every volume. */
struct v7 {
	struct cslot *cs;		/* first: the cheapest to reach */
	struct IOStdReq *ior;
	struct timerequest *treq;
	UBYTE *cmem, *cbuf, *sb;
	ULONG cmsize, unit, lastact, dirtyat;
	int nbuf, hand, last, ndirty, tpend, inhibit;
	struct DosList *vol, *dev;
	struct MsgPort *port;
	struct FileLock *locks;
	struct vfile *files;
	char volname[8];
	int ioerr, sbdirty, die, keepm;
	int dsz, nmax;		/* directory entry and name sizes */
	ULONG fsize;
	LONG dtype;
	LONG err;
};

#define TICKS		(SysBase->ex_TickCount)
#define TICKHZ		100

static void cinval(), unmount();
static int sbread();
#define CBUF(s)		(fs->cbuf + ((UWORD)(s) << 9))

static ULONG
pdp(p)
register UBYTE *p;
{
	return ((ULONG)p[1] << 24 | (ULONG)p[0] << 16 | (UWORD)p[3] << 8 |
	    p[2]);
}

static void
ppdp(p, v)
register UBYTE *p;
ULONG v;
{
	p[0] = v >> 16;
	p[1] = v >> 24;
	p[2] = v;
	p[3] = v >> 8;
}

#define LE16(p)		((p)[0] | (p)[1] << 8)
#define PUT16(p, v)	((p)[0] = (v), (p)[1] = (v) >> 8)

static void
bio(fs, cmd, b, p, n)
register struct v7 *fs;
int cmd, n;
ULONG b;
UBYTE *p;
{
	fs->ior->io_Command = cmd;
	fs->ior->io_Data = (APTR)p;
	fs->ior->io_Length = (ULONG)n << 9;
	fs->ior->io_Offset = b;
	if (DoIO((struct IORequest *)fs->ior) != 0)
		fs->ioerr = 1;
}

/* Block b's buffer, or -1. */
static int
cfind(fs, b)
register struct v7 *fs;
ULONG b;
{
	register struct cslot *c;
	register int i, n;

	c = fs->cs;
	if (c[i = fs->last].c_Blk == b)
		return (i);
	for (i = 0, n = fs->nbuf; i < n; i++, c++)
		if (c->c_Blk == b)
			return (fs->last = i);
	return (-1);
}

/* The system time now, in V7 seconds. */
static ULONG
now()
{
	struct DateStamp ds;

	DateStamp(&ds);
	return (ds.ds_Days * 86400L + ds.ds_Minute * 60L +
	    ds.ds_Tick / TICKS_PER_SECOND + V7EPOCH);
}

static void
sbwrite(fs)
register struct v7 *fs;
{
	ppdp(fs->sb + S_TIME, now());
	bio(fs, CMD_WRITE, 1L, fs->sb, 1);
	fs->sbdirty = 0;
}

/*
 * Write every change, in the order that keeps the disk consistent:
 * file data, the superblock, indirect blocks (single before double),
 * inodes, directory entries; each class in block order, adjacent buffers
 * of adjacent blocks in one transfer.
 */
static void
bsync(fs)
struct v7 *fs;
{
	register struct cslot *c, *cs;
	register int i, m;
	int k, n, cls;

	cs = fs->cs;
	n = fs->nbuf;
	for (cls = C_DATA; cls <= C_DIR && fs->ndirty + fs->sbdirty != 0;
	    cls++) {
		if (cls == C_IND1 && fs->sbdirty)
			sbwrite(fs);
		for (;;) {
			m = -1;
			for (i = 0, c = cs; i < n; i++, c++)
				if (c->c_Class == cls &&
				    (m < 0 || c->c_Blk < cs[m].c_Blk))
					m = i;
			if (m < 0)
				break;
			c = &cs[m];
			for (k = 1; m + k < n && c[k].c_Class == cls &&
			    c[k].c_Blk == c->c_Blk + k; k++)
				;
			bio(fs, CMD_WRITE, c->c_Blk, CBUF(m), k);
			fs->ndirty -= k;
			while (--k >= 0)
				c[k].c_Class = C_CLEAN;
		}
	}
}

/* A buffer to reuse: the next one the hand finds unused since it last
 * passed.  A changed one is written first, with every other change. */
static int
victim(fs)
register struct v7 *fs;
{
	register struct cslot *c;
	register int s, h;
	int n;

	h = fs->hand;
	n = fs->nbuf;
	for (;;) {
		s = h;
		if (++h == n)
			h = 0;
		c = &fs->cs[s];
		if (!c->c_Ref)
			break;
		c->c_Ref = 0;
	}
	fs->hand = h;
	if (c->c_Class != C_CLEAN)
		bsync(fs);
	return (s);
}

/* Block b's cache buffer, read from the disk unless fresh is set (the
 * caller fills it).  A pointer into the cache is good only until the
 * next call that may read a block. */
static UBYTE *
bget(fs, b, fresh)
register struct v7 *fs;
ULONG b;
int fresh;
{
	register struct cslot *c;
	register int s;

	if (fs->cs[s = fs->last].c_Blk != b && (s = cfind(fs, b)) < 0) {
		fs->last = s = victim(fs);
		c = &fs->cs[s];
		c->c_Blk = b;
		if (!fresh) {
			bio(fs, CMD_READ, b, CBUF(s), 1);
			if (fs->ioerr)
				c->c_Blk = 0;
		}
	} else
		c = &fs->cs[s];
	c->c_Ref = 1;
	return (CBUF(s));
}

#define bread(b)	bget(fs, (ULONG)(b), 0)

/* Cached block b has changed, as part of class cls. */
static void
bdirty(fs, b, cls)
register struct v7 *fs;
ULONG b;
int cls;
{
	register struct cslot *c;
	register int s;

	if (fs->cs[s = fs->last].c_Blk != b && (s = cfind(fs, b)) < 0)
		return;			/* it could not be read */
	c = &fs->cs[s];
	if (c->c_Class == C_CLEAN)
		fs->ndirty++;
	c->c_Class = cls;
}

/* Write cached block b now. */
static void
bwrite(fs, b)
register struct v7 *fs;
ULONG b;
{
	register struct cslot *c;
	register int s;

	if ((s = cfind(fs, b)) < 0)
		return;
	c = &fs->cs[s];
	bio(fs, CMD_WRITE, b, CBUF(s), 1);
	if (c->c_Class != C_CLEAN)
		fs->ndirty--;
	c->c_Class = C_CLEAN;
}

static void
bzero(p)
UBYTE *p;
{
	register UWORD *w;
	register int i;

	for (w = (UWORD *)p, i = BLK / 2; i != 0; i--)
		*w++ = 0;
}

static void
iget(fs, ino, ip)
register struct v7 *fs;
UWORD ino;
register struct vino *ip;
{
	register UBYTE *p;
	register int i;

	--ino;
	p = bread(2 + (ino >> 3)) + ((ino & 7) << 6);
	ip->i_Mode = LE16(p);
	ip->i_Nlink = LE16(p + 2);
	ip->i_Size = pdp(p + 8);
	ip->i_Mtime = pdp(p + 56);
	for (i = 0, p += 12; i < 13; i++, p += 3)
		ip->i_Addr[i] = (ULONG)p[0] << 16 | (UWORD)p[2] << 8 | p[1];
}

/* Store ip as inode ino, changed now and, unless keepm is set, modified
 * now; a zero mode clears the inode. */
static void
iput(fs, ino, ip)
register struct v7 *fs;
UWORD ino;
register struct vino *ip;
{
	register UBYTE *p;
	register int i;
	ULONG b, t;

	--ino;
	b = 2 + (ino >> 3);
	p = bread(b) + ((ino & 7) << 6);
	if (ip->i_Mode == 0) {
		for (i = 0; i < 64; i++)
			p[i] = 0;
	} else {
		PUT16(p, ip->i_Mode);
		PUT16(p + 2, ip->i_Nlink);
		ppdp(p + 8, ip->i_Size);
		for (i = 0; i < 13; i++) {
			p[12 + 3 * i] = ip->i_Addr[i] >> 16;
			p[13 + 3 * i] = ip->i_Addr[i];
			p[14 + 3 * i] = ip->i_Addr[i] >> 8;
		}
		t = now();
		ppdp(p + 56, fs->keepm ? ip->i_Mtime : t);
		ppdp(p + 60, t);
	}
	bdirty(fs, b, C_INODE);
}

/* A free block for class cls, 0 when the disk is full.  An indirect
 * block is cleared in the cache, a directory block also on the disk; a
 * data block is left to the caller. */
static ULONG
balloc(fs, cls)
register struct v7 *fs;
int cls;
{
	register UBYTE *p;
	register UWORD n;
	ULONG b;

	n = LE16(fs->sb + S_NFREE);
	if (n == 0 || n > NICFREE ||
	    (b = pdp(fs->sb + S_FREE + (--n << 2))) == 0) {
		fs->err = ERROR_DISK_FULL;
		return (0);
	}
	PUT16(fs->sb + S_NFREE, n);
	ppdp(fs->sb + S_TFREE, pdp(fs->sb + S_TFREE) - 1);
	fs->sbdirty = 1;
	if (n == 0) {
		/* b holds the next part of the list: the superblock takes
		 * it over on the disk before b is reused. */
		p = bread(b);
		PUT16(fs->sb + S_NFREE, LE16(p));
		CopyMem(p + 2, fs->sb + S_FREE, (ULONG)NICFREE * 4);
		sbwrite(fs);
	}
	if (cls != C_DATA) {
		bzero(p = bget(fs, b, 1));
		if (cls == C_DIR)
			bwrite(fs, b);
		else
			bdirty(fs, b, cls);
	}
	return (b);
}

/* Free block b; an empty or full list is spilled into it first.  Nothing
 * on the disk may still point to b. */
static void
bfree(fs, b)
register struct v7 *fs;
ULONG b;
{
	register UBYTE *p;
	register UWORD n;

	n = LE16(fs->sb + S_NFREE);
	if (n == 0 || n >= NICFREE) {
		p = bget(fs, b, 1);
		PUT16(p, n);
		CopyMem(fs->sb + S_FREE, p + 2, (ULONG)NICFREE * 4);
		bwrite(fs, b);
		n = 0;
	}
	ppdp(fs->sb + S_FREE + (n << 2), b);
	PUT16(fs->sb + S_NFREE, n + 1);
	ppdp(fs->sb + S_TFREE, pdp(fs->sb + S_TFREE) + 1);
	fs->sbdirty = 1;
}

/* A free inode, its mode set; 0 when there is none. */
static UWORD
ialloc(fs, mode)
register struct v7 *fs;
UWORD mode;
{
	struct vino i;
	register UWORD n, ino, k, max;

	n = LE16(fs->sb + S_NINODE);
	for (;;) {
		if (n == 0) {
			max = (LE16(fs->sb + S_ISIZE) - 2) * 8;
			for (ino = ROOTINO + 1; ino <= max && n < NICINOD; ino++) {
				iget(fs, ino, &i);
				if (i.i_Mode == 0) {
					k = S_INODE + (n++ << 1);
					PUT16(fs->sb + k, ino);
				}
			}
			if (n == 0) {
				fs->err = ERROR_DISK_FULL;
				return (0);
			}
		}
		ino = LE16(fs->sb + S_INODE + (--n << 1));
		iget(fs, ino, &i);
		if (i.i_Mode == 0)
			break;
	}
	PUT16(fs->sb + S_NINODE, n);
	k = LE16(fs->sb + S_TINODE) - 1;
	PUT16(fs->sb + S_TINODE, k);
	fs->sbdirty = 1;
	for (k = 0; k < 13; k++)
		i.i_Addr[k] = 0;
	i.i_Mode = mode;
	i.i_Nlink = 1;
	i.i_Size = 0;
	iput(fs, ino, &i);
	return (ino);
}

/* Entry i of indirect block b (class bcls); with acls, a missing one is
 * allocated for that class. */
static ULONG
indir(fs, b, bcls, i, acls)
register struct v7 *fs;
ULONG b;
int bcls, acls;
UWORD i;
{
	ULONG v;

	if (b == 0)
		return (0);
	if ((v = pdp(bread(b) + (i << 2))) == 0 && acls &&
	    (v = balloc(fs, acls)) != 0) {
		ppdp(bread(b) + (i << 2), v);
		bdirty(fs, b, bcls);
	}
	return (v);
}

/* Inode address *ap, allocated for class acls when missing. */
#define slot(fs, ap, acls) \
	(*(ap) == 0 && (acls) ? (*(ap) = balloc(fs, acls)) : *(ap))

/* The block holding block n of the file; 0 for a hole, or with alloc set
 * when the disk is full.  Allocation changes ip's addresses. */
static ULONG
bmap(fs, ip, n, alloc)
register struct v7 *fs;
register struct vino *ip;
ULONG n;
int alloc;
{
	ULONG b;
	int dc;

	dc = !alloc ? 0 : (ip->i_Mode & IFMT) == IFDIR ? C_DIR : C_DATA;
	if (n < 10)
		return (slot(fs, &ip->i_Addr[(int)n], dc));
	n -= 10;
	if (n < 128)
		b = slot(fs, &ip->i_Addr[10], alloc ? C_IND1 : 0);
	else {
		n -= 128;
		if (n >= 128L * 128) {
			fs->err = ERROR_OBJECT_TOO_LARGE;
			return (0);
		}
		b = indir(fs, slot(fs, &ip->i_Addr[11], alloc ? C_IND2 : 0),
		    C_IND2, (UWORD)(n >> 7), alloc ? C_IND1 : 0);
	}
	return (indir(fs, b, C_IND1, (UWORD)n & 127, dc));
}

/* Free the blocks under indirect block b (level 1 or 2), then b, last
 * first, so that they are allocated again in their old order. */
static void
freeind(fs, b, level)
register struct v7 *fs;
ULONG b;
int level;
{
	register int i;
	ULONG v;

	for (i = 127; i >= 0; i--)
		if ((v = pdp(bread(b) + (i << 2))) != 0) {
			if (level > 1)
				freeind(fs, v, level - 1);
			else
				bfree(fs, v);
		}
	bfree(fs, b);
}

/* Truncate inode ino (ip) to nothing or, with mode 0, free it: the
 * changed inode reaches the disk before its blocks and the inode are
 * free. */
static void
irelse(fs, ino, ip, mode)
register struct v7 *fs;
UWORD ino;
register struct vino *ip;
UWORD mode;
{
	ULONG a[13];
	register int i;
	register UWORD n;

	for (i = 0; i < 13; i++) {
		a[i] = ip->i_Addr[i];
		ip->i_Addr[i] = 0;
	}
	ip->i_Size = 0;
	ip->i_Mode = mode;
	iput(fs, ino, ip);
	bsync(fs);
	for (i = 12; i >= 0; i--)
		if (a[i] != 0) {
			if (i < 10)
				bfree(fs, a[i]);
			else if (i < 12)
				freeind(fs, a[i], i - 9);
		}
	if (mode == 0) {
		if ((n = LE16(fs->sb + S_NINODE)) < NICINOD) {
			PUT16(fs->sb + S_INODE + (n << 1), ino);
			PUT16(fs->sb + S_NINODE, n + 1);
		}
		n = LE16(fs->sb + S_TINODE) + 1;
		PUT16(fs->sb + S_TINODE, n);
	}
	sbwrite(fs);
}

static int
lower(c)
register int c;
{
	return (c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
}

/* The entry named by the n bytes at name in directory dino: its inode,
 * or 0, and its offset in *pos. */
static UWORD
dfind(fs, dino, name, n, pos)
register struct v7 *fs;
UWORD dino;
char *name;
int n;
ULONG *pos;
{
	struct vino d;
	register UBYTE *e;
	register int i;
	ULONG o;

	iget(fs, dino, &d);
	if ((d.i_Mode & IFMT) != IFDIR)
		return (0);
	if (n > fs->nmax) {
		if (fs->nmax != V7NAME)
			return (0);
		n = fs->nmax;
	}
	for (o = 0; o < d.i_Size; o += fs->dsz) {
		e = bread(bmap(fs, &d, o >> 9, 0)) + ((UWORD)o & (BLK - 1));
		if (LE16(e) == 0)
			continue;
		for (i = 0; i < n && lower(e[2 + i]) == lower(name[i]); i++)
			;
		if (i == n && (n == fs->nmax || e[2 + n] == 0)) {
			*pos = o;
			return (LE16(e));
		}
	}
	return (0);
}

static UWORD
lookup(fs, dino, name, n)
register struct v7 *fs;
UWORD dino;
char *name;
int n;
{
	ULONG pos;

	return (dfind(fs, dino, name, n, &pos));
}

/* Set the entry at pos of directory dino to ino and, unless name is
 * NULL, to the n bytes at name. */
static void
dset(fs, dino, pos, ino, name, n)
register struct v7 *fs;
UWORD dino;
ULONG pos;
UWORD ino;
char *name;
int n;
{
	struct vino d;
	register UBYTE *e;
	register int i;
	ULONG b;

	iget(fs, dino, &d);
	b = bmap(fs, &d, pos >> 9, 0);
	e = bread(b) + ((UWORD)pos & (BLK - 1));
	PUT16(e, ino);
	if (name != NULL)
		for (i = 0; i < fs->nmax; i++)
			e[2 + i] = i < n ? name[i] : 0;
	bdirty(fs, b, C_DIR);
}

/* Add the entry name -> ino to directory dino: 0 when it is full. */
static int
dadd(fs, dino, name, n, ino)
register struct v7 *fs;
UWORD dino;
char *name;
int n;
UWORD ino;
{
	struct vino d;
	ULONG pos;

	iget(fs, dino, &d);
	for (pos = 0; pos < d.i_Size; pos += fs->dsz)
		if (LE16(bread(bmap(fs, &d, pos >> 9, 0)) +
		    ((UWORD)pos & (BLK - 1))) == 0)
			break;
	if (pos == d.i_Size) {
		if (bmap(fs, &d, pos >> 9, 1) == 0)
			return (0);
		d.i_Size += fs->dsz;
		iput(fs, dino, &d);
	}
	dset(fs, dino, pos, ino, name, n);
	return (1);
}

/* 1 when directory dino holds nothing but "." and "..". */
static int
dempty(fs, dino)
register struct v7 *fs;
UWORD dino;
{
	struct vino d;
	register UBYTE *e;
	ULONG pos;

	iget(fs, dino, &d);
	for (pos = fs->dsz << 1; pos < d.i_Size; pos += fs->dsz) {
		e = bread(bmap(fs, &d, pos >> 9, 0)) + ((UWORD)pos & (BLK - 1));
		if (LE16(e) != 0)
			return (0);
	}
	return (1);
}

/* Walk the len bytes of path from the key's inode ('/' alone or
 * doubled is the parent): the key of what it names, or 0 with err set. */
static ULONG
walk(fs, key, path, len)
struct v7 *fs;
ULONG key;
register char *path;
int len;
{
	register char *p, *end;
	register UWORD ino, par;
	struct vino i;

	end = path + len;
	for (p = path; p < end; p++)
		if (*p == ':')
			path = p + 1;
	ino = key;
	par = key >> 16;
	while (path < end) {
		iget(fs, ino, &i);
		if ((i.i_Mode & IFMT) != IFDIR) {
			fs->err = ERROR_OBJECT_WRONG_TYPE;
			return (0);
		}
		if (*path == '/') {
			ino = lookup(fs, ino, "..", 2);
			par = lookup(fs, ino, "..", 2);
			path++;
			continue;
		}
		for (p = path; p < end && *p != '/'; p++)
			;
		par = ino;
		if ((ino = lookup(fs, ino, path, (int)(p - path))) == 0) {
			fs->err = p == end ? ERROR_OBJECT_NOT_FOUND :
			    ERROR_DIR_NOT_FOUND;
			return (0);
		}
		path = p < end ? p + 1 : p;
	}
	return ((ULONG)par << 16 | ino);
}

static int
slen(s)
register char *s;
{
	register int n;

	for (n = 0; s[n] != '\0'; n++)
		;
	return (n);
}

/* The directory that path's last part goes in: its inode, or 0 with err
 * set; *name and *n are the last part. */
static UWORD
parent(fs, key, path, name, n)
register struct v7 *fs;
ULONG key;
char *path, **name;
int *n;
{
	register char *p, *s;
	ULONG k;

	for (p = s = path; *p != '\0'; p++)
		if (*p == ':' || *p == '/')
			s = p + 1;
	*name = s;
	*n = slen(s);
	if (*n == 0 || (s[0] == '.' && (*n == 1 || (*n == 2 && s[1] == '.')))) {
		fs->err = ERROR_OBJECT_WRONG_TYPE;
		return (0);
	}
	if (*n > fs->nmax && fs->nmax != V7NAME) {
		fs->err = ERROR_INVALID_COMPONENT_NAME;
		return (0);
	}
	if ((k = walk(fs, key, path, (int)(s - path))) == 0)
		return (0);
	return ((UWORD)k);
}

/* Uses of inode ino: 1 for each, 0x100 when one is exclusive. */
static int
inuse(fs, ino)
register struct v7 *fs;
UWORD ino;
{
	register struct FileLock *l;
	register struct vfile *f;
	register int n;

	n = 0;
	for (l = fs->locks; l != NULL; l = (struct FileLock *)l->fl_Link)
		if ((UWORD)l->fl_Key == ino)
			n += l->fl_Access == EXCLUSIVE_LOCK ? 0x101 : 1;
	for (f = fs->files; f != NULL; f = f->f_Next)
		if ((UWORD)f->f_Key == ino)
			n += f->f_Excl ? 0x101 : 1;
	return (n);
}

/* May inode ino be used, exclusively if excl is set? */
static int
canuse(fs, ino, excl)
register struct v7 *fs;
UWORD ino;
int excl;
{
	register int n;

	n = inuse(fs, ino);
	if (excl ? n != 0 : (n & 0x100) != 0) {
		fs->err = ERROR_OBJECT_IN_USE;
		return (0);
	}
	return (1);
}

/* The key's name: the volume's for the root, else its directory entry. */
static void
nameof(fs, key, buf)
struct v7 *fs;
ULONG key;
register char *buf;
{
	struct vino d;
	register UBYTE *e;
	register int i;
	register UWORD ino;
	ULONG pos;

	*buf = '\0';
	if ((ino = key) == ROOTINO) {
		for (i = 0; (buf[i] = fs->volname[i]) != '\0'; i++)
			;
		return;
	}
	iget(fs, (UWORD)(key >> 16), &d);
	for (pos = fs->dsz << 1; pos < d.i_Size; pos += fs->dsz) {
		e = bread(bmap(fs, &d, pos >> 9, 0)) + ((UWORD)pos & (BLK - 1));
		if (LE16(e) == ino) {
			for (i = 0; i < fs->nmax && e[2 + i] != 0; i++)
				buf[i] = e[2 + i];
			buf[i] = '\0';
			return;
		}
	}
}

/* V7 time to a DateStamp. */
static void
v7date(t, ds)
ULONG t;
register struct DateStamp *ds;
{
	t = t > V7EPOCH ? t - V7EPOCH : 0;
	ds->ds_Days = t / 86400L;
	t %= 86400L;
	ds->ds_Minute = t / 60;
	ds->ds_Tick = (t % 60) * TICKS_PER_SECOND;
}

static void
fillfib(fs, fib, key, name)
register struct v7 *fs;
register struct FileInfoBlock *fib;
ULONG key;
char *name;
{
	struct vino i;
	register int k;
	register LONG t;
	UWORD ino;

	iget(fs, ino = key, &i);
	fib->fib_DiskKey = 0;
	t = (i.i_Mode & IFMT) != IFDIR ? ST_FILE :
	    ino == ROOTINO ? ST_ROOT : ST_USERDIR;
	fib->fib_DirEntryType = fib->fib_EntryType = t;
	if (name != NULL) {
		for (k = 0; k < fs->nmax && name[k] != '\0'; k++)
			fib->fib_FileName[k] = name[k];
		fib->fib_FileName[k] = '\0';
	} else
		nameof(fs, key, fib->fib_FileName);
	t = 0;
	if (!(i.i_Mode & 0400))
		t |= FIBF_READ;
	if (!(i.i_Mode & 0200))
		t |= FIBF_WRITE | FIBF_DELETE;
	if (!(i.i_Mode & 0100))
		t |= FIBF_EXECUTE;
	fib->fib_Protection = t;
	fib->fib_Size = (i.i_Mode & IFMT) == IFDIR ? 0 : i.i_Size;
	fib->fib_NumBlocks = (i.i_Size + BLK - 1) / BLK;
	v7date(i.i_Mtime, &fib->fib_Date);
	fib->fib_Comment[0] = '\0';
	fib->fib_OwnerUID = fib->fib_OwnerGID = 0;
}

static struct FileLock *
newlock(fs, key, mode)
register struct v7 *fs;
ULONG key;
LONG mode;
{
	register struct FileLock *l;

	if (!canuse(fs, (UWORD)key, mode == EXCLUSIVE_LOCK))
		return (NULL);
	if ((l = (struct FileLock *)AllocMem((ULONG)sizeof(*l),
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
		fs->err = ERROR_NO_FREE_STORE;
		return (NULL);
	}
	l->fl_Key = key;
	l->fl_Access = mode == EXCLUSIVE_LOCK ? EXCLUSIVE_LOCK : SHARED_LOCK;
	l->fl_Task = fs->port;
	l->fl_Volume = (BPTR)fs->vol;
	l->fl_Link = (BPTR)fs->locks;
	fs->locks = l;
	return (l);
}

static void
freelock(fs, l)
register struct v7 *fs;
struct FileLock *l;
{
	register struct FileLock **pp;

	for (pp = &fs->locks; *pp != NULL;
	    pp = (struct FileLock **)&(*pp)->fl_Link)
		if (*pp == l) {
			*pp = (struct FileLock *)l->fl_Link;
			FreeMem(l, (ULONG)sizeof(*l));
			return;
		}
}

#define LKKEY(l)	((l) != 0 ? ((struct FileLock *)(l))->fl_Key : ROOTKEY)

/* Open or create (act FINDOUTPUT truncates, FINDUPDATE keeps) the file
 * path names from lock: its vfile, or NULL with err set. */
static struct vfile *
fopen(fs, act, lock, path)
register struct v7 *fs;
LONG act, lock;
char *path;
{
	register struct vfile *f;
	struct vino i;
	ULONG key;
	UWORD dir, ino;
	char *name;
	int n, excl;

	excl = act == ACTION_FINDOUTPUT;
	key = walk(fs, LKKEY(lock), path, slen(path));
	if (key == 0 && act != ACTION_FINDINPUT &&
	    fs->err == ERROR_OBJECT_NOT_FOUND) {
		if ((dir = parent(fs, LKKEY(lock), path, &name, &n)) == 0 ||
		    (ino = ialloc(fs, IFREG | 0644)) == 0)
			return (NULL);
		if (!dadd(fs, dir, name, n, ino)) {
			iget(fs, ino, &i);
			irelse(fs, ino, &i, 0);
			return (NULL);
		}
		key = (ULONG)dir << 16 | ino;
	}
	if (key == 0)
		return (NULL);
	iget(fs, (UWORD)key, &i);
	if ((i.i_Mode & IFMT) != IFREG) {
		fs->err = ERROR_OBJECT_WRONG_TYPE;
		return (NULL);
	}
	if (!canuse(fs, (UWORD)key, excl))
		return (NULL);
	if ((f = (struct vfile *)AllocMem((ULONG)sizeof(*f),
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
		fs->err = ERROR_NO_FREE_STORE;
		return (NULL);
	}
	if (excl && i.i_Size != 0)
		irelse(fs, (UWORD)key, &i, i.i_Mode);
	f->f_Key = key;
	f->f_Excl = excl;
	f->f_Next = fs->files;
	fs->files = f;
	return (f);
}

static void
fclose(fs, f)
register struct v7 *fs;
struct vfile *f;
{
	register struct vfile **pp;

	for (pp = &fs->files; *pp != NULL; pp = &(*pp)->f_Next)
		if (*pp == f) {
			*pp = f->f_Next;
			FreeMem(f, (ULONG)sizeof(*f));
			return;
		}
}

/* Block b, block nb of file ip, for reading.  On a miss, the blocks of
 * the file that follow it on the disk come with it in one transfer, into
 * the buffers after its own. */
static UBYTE *
bra(fs, ip, nb, b)
struct v7 *fs;
register struct vino *ip;
ULONG nb, b;
{
	register struct cslot *c;
	register int k, j;
	int s, n;

	if (fs->cs[s = fs->last].c_Blk != b && (s = cfind(fs, b)) < 0) {
		for (k = 1; k < RAMAX && ((nb + k) << 9) < ip->i_Size &&
		    bmap(fs, ip, nb + k, 0) == b + k; k++)
			;
		fs->last = s = victim(fs);
		n = fs->nbuf;
		for (j = 1, c = &fs->cs[s]; j < k && s + j < n; j++)
			if (c[j].c_Class != C_CLEAN || c[j].c_Ref ||
			    cfind(fs, b + j) >= 0)
				break;
		for (k = 0; k < j; k++)
			c[k].c_Blk = b + k;
		bio(fs, CMD_READ, b, CBUF(s), j);
		if (fs->ioerr)
			for (k = 0; k < j; k++)
				c[k].c_Blk = 0;
		fs->hand = s + j < n ? s + j : 0;
		fs->last = s;
	}
	fs->cs[s].c_Ref = 1;
	return (CBUF(s));
}

/* Read (wr 0) or write len bytes at the file's position. */
static LONG
fio(fs, f, buf, len, wr)
register struct v7 *fs;
register struct vfile *f;
char *buf;
LONG len;
int wr;
{
	struct vino i;
	register UWORD o, k;
	UBYTE *p;
	ULONG b, nb;
	LONG n;
	int fresh;

	iget(fs, (UWORD)f->f_Key, &i);
	if (!wr) {
		if (f->f_Pos >= i.i_Size)
			return (0);
		if (len > i.i_Size - f->f_Pos)
			len = i.i_Size - f->f_Pos;
	}
	for (n = 0; n < len; n += k) {
		o = (UWORD)f->f_Pos & (BLK - 1);
		k = BLK - o;
		if (k > len - n)
			k = len - n;
		nb = f->f_Pos >> 9;
		b = bmap(fs, &i, nb, wr);
		if (b == 0 && wr)
			break;
		if (b == 0) {
			for (o = 0; o < k; o++)
				buf[o] = 0;
		} else if (wr) {
			/* A block past the end is new: nothing to read. */
			fresh = f->f_Pos - o >= i.i_Size;
			p = bget(fs, b, fresh || k == BLK);
			if (fresh && k != BLK)
				bzero(p);
			CopyMem(buf, p + o, (ULONG)k);
			bdirty(fs, b, C_DATA);
		} else
			CopyMem(bra(fs, &i, nb, b) + o, buf, (ULONG)k);
		buf = (char *)PtrStep(buf, (LONG)k);
		f->f_Pos += k;
		if (f->f_Pos > i.i_Size)
			i.i_Size = f->f_Pos;
	}
	if (wr)
		iput(fs, (UWORD)f->f_Key, &i);
	if (fs->ioerr)
		return (-1);
	return (n == 0 && len != 0 && wr ? -1L : n);
}

static LONG
delete(fs, lock, path)
register struct v7 *fs;
LONG lock;
char *path;
{
	struct vino i, d;
	UWORD dir, ino;
	ULONG pos;
	char *name;
	int n;

	if ((dir = parent(fs, LKKEY(lock), path, &name, &n)) == 0)
		return (DOSFALSE);
	if ((ino = dfind(fs, dir, name, n, &pos)) == 0) {
		fs->err = ERROR_OBJECT_NOT_FOUND;
		return (DOSFALSE);
	}
	if (inuse(fs, ino)) {
		fs->err = ERROR_OBJECT_IN_USE;
		return (DOSFALSE);
	}
	iget(fs, ino, &i);
	if ((i.i_Mode & IFMT) == IFDIR) {
		if (!dempty(fs, ino)) {
			fs->err = ERROR_DIRECTORY_NOT_EMPTY;
			return (DOSFALSE);
		}
		i.i_Nlink = 1;
		iget(fs, dir, &d);
		d.i_Nlink--;
		iput(fs, dir, &d);
	}
	dset(fs, dir, pos, 0, (char *)NULL, 0);
	if (--i.i_Nlink == 0) {
		bsync(fs);		/* the entry goes first */
		irelse(fs, ino, &i, 0);
	} else
		iput(fs, ino, &i);
	return (DOSTRUE);
}

static LONG
mkdir(fs, lock, path)
register struct v7 *fs;
LONG lock;
char *path;
{
	struct vino d;
	UWORD dir, ino;
	char *name;
	int n, ok;

	if ((dir = parent(fs, LKKEY(lock), path, &name, &n)) == 0)
		return (DOSFALSE);
	if (lookup(fs, dir, name, n) != 0) {
		fs->err = ERROR_OBJECT_EXISTS;
		return (DOSFALSE);
	}
	if ((ino = ialloc(fs, IFDIR | 0755)) == 0)
		return (DOSFALSE);
	ok = dadd(fs, ino, ".", 1, ino) && dadd(fs, ino, "..", 2, dir);
	iget(fs, ino, &d);
	if (ok) {
		/* Its entries reach the disk before any name for it. */
		bwrite(fs, d.i_Addr[0]);
		ok = dadd(fs, dir, name, n, ino);
	}
	if (!ok) {
		irelse(fs, ino, &d, 0);
		return (DOSFALSE);
	}
	d.i_Nlink = 2;
	iput(fs, ino, &d);
	iget(fs, dir, &d);
	d.i_Nlink++;
	iput(fs, dir, &d);
	return ((LONG)newlock(fs, (ULONG)dir << 16 | ino, (LONG)SHARED_LOCK));
}

/* Rename within the volume; a directory may not move into itself. */
static LONG
rename(fs, l1, p1, l2, p2)
register struct v7 *fs;
LONG l1, l2;
char *p1, *p2;
{
	struct vino i, d;
	UWORD d1, d2, ino, x, k;
	ULONG pos, pos2;
	char *n1, *n2;
	int k1, k2;

	if ((d1 = parent(fs, LKKEY(l1), p1, &n1, &k1)) == 0 ||
	    (d2 = parent(fs, LKKEY(l2), p2, &n2, &k2)) == 0)
		return (DOSFALSE);
	if ((ino = dfind(fs, d1, n1, k1, &pos)) == 0) {
		fs->err = ERROR_OBJECT_NOT_FOUND;
		return (DOSFALSE);
	}
	if ((x = dfind(fs, d2, n2, k2, &pos2)) != 0) {
		if (x != ino || d1 != d2) {
			fs->err = ERROR_OBJECT_EXISTS;
			return (DOSFALSE);
		}
		dset(fs, d1, pos, ino, n2, k2);	/* a new spelling */
		return (DOSTRUE);
	}
	iget(fs, ino, &i);
	if ((i.i_Mode & IFMT) == IFDIR && d1 != d2)
		for (x = d2; ; x = k) {
			if (x == ino) {
				fs->err = ERROR_OBJECT_IN_USE;
				return (DOSFALSE);
			}
			if (x == ROOTINO || (k = lookup(fs, x, "..", 2)) == x)
				break;
		}
	if (!dadd(fs, d2, n2, k2, ino))
		return (DOSFALSE);
	bsync(fs);			/* the new name before the old goes */
	dset(fs, d1, pos, 0, (char *)NULL, 0);
	if ((i.i_Mode & IFMT) == IFDIR && d1 != d2) {
		dfind(fs, ino, "..", 2, &pos);
		dset(fs, ino, pos, d2, (char *)NULL, 0);
		iget(fs, d1, &d);
		d.i_Nlink--;
		iput(fs, d1, &d);
		iget(fs, d2, &d);
		d.i_Nlink++;
		iput(fs, d2, &d);
	}
	return (DOSTRUE);
}

static void
info(fs, id)
register struct v7 *fs;
register struct InfoData *id;
{
	id->id_NumSoftErrors = 0;
	id->id_UnitNumber = fs->unit;
	id->id_DiskState = ID_VALIDATED;
	id->id_NumBlocks = fs->fsize;
	id->id_NumBlocksUsed = fs->fsize - pdp(fs->sb + S_TFREE);
	id->id_BytesPerBlock = BLK;
	id->id_DiskType = fs->dtype;
	id->id_VolumeNode = (BPTR)fs->vol;
	id->id_InUse = fs->locks != NULL || fs->files != NULL ? DOSTRUE :
	    DOSFALSE;
}

/* SetProtection (date 0) or SetFileDate on the object path names from
 * lock: the owner's mode bits from the clear R, W and D, E bits, or the
 * modification time from the DateStamp. */
static LONG
setattr(fs, lock, path, prot, date)
register struct v7 *fs;
LONG lock, prot;
char *path;
register struct DateStamp *date;
{
	struct vino i;
	ULONG key;
	register UWORD m;

	if ((key = walk(fs, LKKEY(lock), path, slen(path))) == 0)
		return (DOSFALSE);
	iget(fs, (UWORD)key, &i);
	if (date != NULL)
		i.i_Mtime = date->ds_Days * 86400L + date->ds_Minute * 60 +
		    date->ds_Tick / TICKS_PER_SECOND + V7EPOCH;
	else {
		m = i.i_Mode & ~0700;
		if (!(prot & FIBF_READ))
			m |= 0400;
		if (!(prot & (FIBF_WRITE | FIBF_DELETE)))
			m |= 0200;
		if (!(prot & FIBF_EXECUTE))
			m |= 0100;
		i.i_Mode = m;
	}
	fs->keepm = 1;
	iput(fs, (UWORD)key, &i);
	fs->keepm = 0;
	return (DOSTRUE);
}

static void
packet(fs, dp)
register struct v7 *fs;
register struct DosPacket *dp;
{
	register struct vfile *f;
	LONG r1, a;
	ULONG key;
	UWORD ino;

	r1 = DOSTRUE;
	fs->err = 0;
	fs->ioerr = 0;
	fs->die = 0;
	f = (struct vfile *)dp->dp_Arg1;
	if (fs->inhibit && dp->dp_Type != ACTION_INHIBIT)
		fs->ioerr = 1;
	else switch ((int)dp->dp_Type) {
	case ACTION_LOCATE_OBJECT:
		key = walk(fs, LKKEY(dp->dp_Arg1), (char *)dp->dp_Arg2,
		    slen((char *)dp->dp_Arg2));
		r1 = key == 0 ? 0L : (LONG)newlock(fs, key, dp->dp_Arg3);
		break;
	case ACTION_COPY_DIR:
		r1 = (LONG)newlock(fs, LKKEY(dp->dp_Arg1), (LONG)SHARED_LOCK);
		break;
	case ACTION_PARENT:
		key = LKKEY(dp->dp_Arg1);
		ino = key >> 16;
		r1 = (UWORD)key == ROOTINO ? 0L : (LONG)newlock(fs,
		    (ULONG)lookup(fs, ino, "..", 2) << 16 | ino,
		    (LONG)SHARED_LOCK);
		break;
	case ACTION_FREE_LOCK:
		if (dp->dp_Arg1 != 0)
			freelock(fs, (struct FileLock *)dp->dp_Arg1);
		break;
	case ACTION_SAME_LOCK:
		r1 = (UWORD)LKKEY(dp->dp_Arg1) == (UWORD)LKKEY(dp->dp_Arg2) ?
		    DOSTRUE : DOSFALSE;
		break;
	case ACTION_EXAMINE_OBJECT:
		fillfib(fs, (struct FileInfoBlock *)dp->dp_Arg2,
		    LKKEY(dp->dp_Arg1), (char *)NULL);
		break;
	case ACTION_EXAMINE_NEXT: {
		struct FileInfoBlock *fib;
		struct vino d;
		register UBYTE *e;
		char nm[NAMEMAX + 1];
		int i;

		fib = (struct FileInfoBlock *)dp->dp_Arg2;
		key = LKKEY(dp->dp_Arg1);
		iget(fs, (UWORD)key, &d);
		r1 = DOSFALSE;
		fs->err = ERROR_NO_MORE_ENTRIES;
		if ((d.i_Mode & IFMT) != IFDIR)
			break;
		for (a = fib->fib_DiskKey; a < d.i_Size; a += fs->dsz) {
			e = bread(bmap(fs, &d, (ULONG)a >> 9, 0)) +
			    ((UWORD)a & (BLK - 1));
			if (LE16(e) == 0 || (e[2] == '.' && (e[3] == 0 ||
			    (e[3] == '.' && e[4] == 0))))
				continue;
			for (i = 0; i < fs->nmax; i++)
				nm[i] = e[2 + i];
			nm[fs->nmax] = '\0';
			fillfib(fs, fib, key << 16 | LE16(e), nm);
			fib->fib_DiskKey = a + fs->dsz;
			r1 = DOSTRUE;
			fs->err = 0;
			break;
		}
		break;
	}
	case ACTION_FINDINPUT:
	case ACTION_FINDOUTPUT:
	case ACTION_FINDUPDATE:
		if ((f = fopen(fs, dp->dp_Type, dp->dp_Arg2,
		    (char *)dp->dp_Arg3)) == NULL)
			r1 = DOSFALSE;
		else
			((struct FileHandle *)dp->dp_Arg1)->fh_Args = (LONG)f;
		break;
	case ACTION_READ:
	case ACTION_WRITE:
		r1 = fio(fs, f, (char *)dp->dp_Arg2, dp->dp_Arg3,
		    dp->dp_Type == ACTION_WRITE);
		break;
	case ACTION_SEEK: {
		struct vino i;

		iget(fs, (UWORD)f->f_Key, &i);
		a = dp->dp_Arg2;
		if (dp->dp_Arg3 == OFFSET_CURRENT)
			a += f->f_Pos;
		else if (dp->dp_Arg3 == OFFSET_END)
			a += i.i_Size;
		if (a < 0 || a > i.i_Size) {
			r1 = -1;
			fs->err = ERROR_SEEK_ERROR;
			break;
		}
		r1 = f->f_Pos;
		f->f_Pos = a;
		break;
	}
	case ACTION_END:
		fclose(fs, f);
		bsync(fs);
		break;
	case ACTION_EXAMINE_FH:
		fillfib(fs, (struct FileInfoBlock *)dp->dp_Arg2, f->f_Key,
		    (char *)NULL);
		break;
	case ACTION_DELETE_OBJECT:
		r1 = delete(fs, dp->dp_Arg1, (char *)dp->dp_Arg2);
		break;
	case ACTION_CREATE_DIR:
		r1 = mkdir(fs, dp->dp_Arg1, (char *)dp->dp_Arg2);
		break;
	case ACTION_RENAME_OBJECT:
		r1 = rename(fs, dp->dp_Arg1, (char *)dp->dp_Arg2, dp->dp_Arg3,
		    (char *)dp->dp_Arg4);
		break;
	case ACTION_SET_PROTECT:
		r1 = setattr(fs, dp->dp_Arg2, (char *)dp->dp_Arg3, dp->dp_Arg4,
		    (struct DateStamp *)NULL);
		break;
	case ACTION_SET_DATE:
		r1 = setattr(fs, dp->dp_Arg2, (char *)dp->dp_Arg3, 0L,
		    (struct DateStamp *)dp->dp_Arg4);
		break;
	case ACTION_INFO:
		info(fs, (struct InfoData *)dp->dp_Arg2);
		break;
	case ACTION_DISK_INFO:
		info(fs, (struct InfoData *)dp->dp_Arg1);
		break;
	case ACTION_CURRENT_VOLUME:
		r1 = (LONG)fs->vol;
		break;
	case ACTION_IS_FILESYSTEM:
		break;
	case ACTION_FLUSH:
		bsync(fs);
		break;
	case ACTION_INHIBIT:
		/* Inhibited, the disk is left alone; then it is read anew. */
		if (dp->dp_Arg1 != DOSFALSE) {
			if (fs->inhibit++ == 0) {
				bsync(fs);
				cinval(fs);
			}
		} else if (fs->inhibit != 0 && --fs->inhibit == 0 &&
		    !sbread(fs))
			fs->ioerr = 1;
		break;
	case ACTION_DIE:
		if (fs->locks != NULL || fs->files != NULL) {
			r1 = DOSFALSE;
			fs->err = ERROR_OBJECT_IN_USE;
		} else {
			bsync(fs);
			fs->die = 1;
		}
		break;
	default:
		r1 = DOSFALSE;
		fs->err = ERROR_ACTION_NOT_KNOWN;
	}
	fs->lastact = TICKS;
	if (fs->ioerr) {
		r1 = dp->dp_Type == ACTION_READ ||
		    dp->dp_Type == ACTION_WRITE ||
		    dp->dp_Type == ACTION_SEEK ? -1L : DOSFALSE;
		fs->err = ERROR_NOT_A_DOS_DISK;
	}
	if (fs->die) {
		/* The device has no handler again from the reply on. */
		unmount(fs);
		Forbid();
		fs->dev->dol_Task = NULL;
	}
	ReplyPkt(dp, r1, r1 == DOSFALSE || r1 == -1 ? fs->err : 0L);
}

/* Read the superblock and the kind of filesystem: 0 when it is
 * neither. */
static int
sbread(fs)
register struct v7 *fs;
{
	register UBYTE *m;

	bio(fs, CMD_READ, 1L, fs->sb, 1);
	m = fs->sb + S_LMAGIC;
	fs->dsz = 16;
	fs->nmax = V7NAME;
	fs->dtype = ID_V7_DISK;
	fs->fsize = pdp(fs->sb + S_FSIZE);
	if (m[0] == 'E' && m[1] == '9' && m[2] == 'L' && m[3] == 'N' &&
	    LE16(fs->sb + S_LDSIZE) == 32) {
		fs->dsz = 32;
		fs->nmax = NAMEMAX;
		fs->dtype = ID_V7L_DISK;
		fs->fsize = pdp(fs->sb + S_LFSIZE);
	}
	return (!fs->ioerr && fs->fsize != 0);
}

static void
cinval(fs)
register struct v7 *fs;
{
	register int i;

	for (i = 0; i < fs->nbuf; i++)
		fs->cs[i].c_Blk = 0;
	fs->hand = fs->last = 0;
}

static void
tarm(fs)
register struct v7 *fs;
{
	fs->treq->tr_node.io_Command = TR_ADDREQUEST;
	fs->treq->tr_time.tv_secs = IDLEDLY / TICKHZ;
	fs->treq->tr_time.tv_micro = 0;
	SendIO((struct IORequest *)fs->treq);
	fs->tpend = 1;
}

/* A request of size bytes on a new port. */
static struct IORequest *
newio(size)
int size;
{
	return (CreateIORequest(CreateMsgPort(), (ULONG)size));
}

/* Free r and its port, closing its device first if open is set. */
static void
freeio(r, open)
register struct IORequest *r;
int open;
{
	struct MsgPort *mp;

	mp = r->io_Message.mn_ReplyPort;
	if (open)
		CloseDevice(r);
	DeleteIORequest(r);
	DeleteMsgPort(mp);
}

/*
 * Mount: dol_Startup is the device's FileSysStartupMsg.  The cache is
 * de_NumBuffers buffers of de_BufMemType memory after the superblock's,
 * placed on a 512-byte physical boundary so that wd.device moves them
 * by DMA, several at a time.
 */
static int
mount(fs, dp)
register struct v7 *fs;
register struct DosPacket *dp;
{
	register struct FileSysStartupMsg *fm;
	register struct DosEnvec *de;
	register int i;
	ULONG mt, len, ph, n;

	fm = (struct FileSysStartupMsg *)dp->dp_Arg2;
	de = fm->fssm_Environ;
	fs->dev = (struct DosList *)dp->dp_Arg3;
	fs->unit = fm->fssm_Unit;
	n = de->de_TableSize >= DE_NUMBUFFERS ? de->de_NumBuffers : NBUFDEF;
	if (n < NBUFMIN)
		n = NBUFMIN;
	if (n > NBUFMAX)
		n = NBUFMAX;
	fs->nbuf = n;
	mt = de->de_TableSize >= DE_BUFMEMTYPE ? de->de_BufMemType : MEMF_ANY;
	fs->cmsize = (n + 2) * BLK - 2;
	if ((fs->cmem = (UBYTE *)AllocMem(fs->cmsize, mt)) == NULL ||
	    (fs->cs = (struct cslot *)AllocMem(n * sizeof(*fs->cs),
	    MEMF_ANY | MEMF_CLEAR)) == NULL ||
	    (fs->ior = (struct IOStdReq *)newio(sizeof(*fs->ior))) == NULL ||
	    OpenDevice(fm->fssm_Device, fs->unit, fs->ior, fm->fssm_Flags) != 0)
		return (0);
	len = 1;
	ph = (ULONG)CachePreDMA(fs->cmem, &len, 0L);
	fs->sb = fs->cmem + (ph == 0xffffffffL ? 0 : -(UWORD)ph & (BLK - 1));
	fs->cbuf = fs->sb + BLK;
	cinval(fs);
	fs->ndirty = fs->sbdirty = fs->tpend = fs->inhibit = 0;
	if ((fs->treq = (struct timerequest *)newio(sizeof(*fs->treq))) !=
	    NULL &&
	    OpenDevice(TIMERNAME, (ULONG)UNIT_VBLANK, fs->treq, 0L) != 0) {
		freeio(fs->treq, 0);
		fs->treq = NULL;
	}
	if (!sbread(fs))
		return (0);
	for (i = 0; i < 6 && fs->sb[S_FNAME + i] > ' '; i++)
		fs->volname[i] = fs->sb[S_FNAME + i];
	fs->volname[i] = '\0';
	if ((fs->vol = MakeDosEntry(fs->volname, (LONG)DLT_VOLUME)) == NULL)
		return (0);
	fs->vol->dol_Task = fs->port;
	fs->vol->dol_misc.dol_volume.dol_DiskType = fs->dtype;
	v7date(pdp(fs->sb + S_TIME),
	    &fs->vol->dol_misc.dol_volume.dol_VolumeDate);
	AddDosEntry(fs->vol);
	fs->dev->dol_Task = fs->port;
	return (1);
}

/* ACTION_DIE: everything mount made goes. */
static void
unmount(fs)
register struct v7 *fs;
{
	if (fs->treq != NULL) {
		if (fs->tpend) {
			AbortIO((struct IORequest *)fs->treq);
			WaitIO((struct IORequest *)fs->treq);
		}
		freeio(fs->treq, 1);
		fs->treq = NULL;
	}
	freeio(fs->ior, 1);
	RemDosEntry(fs->vol);
	FreeDosEntry(fs->vol);
	FreeMem(fs->cs, (ULONG)fs->nbuf * sizeof(*fs->cs));
	FreeMem(fs->cmem, fs->cmsize);
}

/*
 * Packets are served as they come.  While changes wait, a timer request
 * runs: a second after the last packet, or when a change has waited
 * MAXDLY ticks, the cache is written back.
 */
int
main()
{
	register struct Message *m;
	register struct DosPacket *dp;
	register struct v7 *fs;
	struct MsgPort *tp;
	ULONG sigs;
	int aging;

	if (DOSBase == NULL)		/* a copy LoadSeg made */
		DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 39L);
	dp = WaitPkt();
	if ((fs = (struct v7 *)AllocMem((ULONG)sizeof(*fs),
	    MEMF_PUBLIC | MEMF_CLEAR)) == NULL) {
		ReplyPkt(dp, DOSFALSE, ERROR_NO_FREE_STORE);
		return (0);
	}
	fs->port = &((struct Process *)FindTask(NULL))->pr_MsgPort;
	if (!mount(fs, dp)) {
		FreeMem(fs, (ULONG)sizeof(*fs));
		ReplyPkt(dp, DOSFALSE, ERROR_NO_DISK);
		return (0);
	}
	ReplyPkt(dp, DOSTRUE, 0L);
	tp = fs->treq != NULL ? fs->treq->tr_node.io_Message.mn_ReplyPort :
	    NULL;
	sigs = 1L << fs->port->mp_SigBit;
	if (tp != NULL)
		sigs |= 1L << tp->mp_SigBit;
	aging = 0;
	for (;;) {
		if ((m = GetMsg(fs->port)) != NULL) {
			packet(fs, (struct DosPacket *)m->mn_Node.ln_Name);
			if (fs->die) {
				FreeMem(fs, (ULONG)sizeof(*fs));
				return (0);
			}
		} else if (tp != NULL && GetMsg(tp) != NULL) {
			fs->tpend = 0;
			if (TICKS - fs->lastact >= IDLEDLY ||
			    TICKS - fs->dirtyat >= MAXDLY)
				bsync(fs);
		} else {
			Wait(sigs);
			continue;
		}
		if (fs->ndirty + fs->sbdirty == 0)
			aging = 0;
		else if (fs->treq != NULL && !fs->tpend) {
			if (!aging) {
				aging = 1;
				fs->dirtyat = TICKS;
			}
			tarm(fs);
		}
	}
}

static char fsname[] = "v7fs-handler";
static char fsid[] = "v7fs-handler 39.3 (Exec900)";

static struct DosEnvec fsenv = {
	DE_BUFMEMTYPE, BLK / 4, 0, 0, 1, 0, 0, 0, 0, 0, 0, NBUFDEF, MEMF_ANY
};
/* DH1's environment, after a tag by which mkdisk.py --sysbuffers finds
 * its de_NumBuffers in the hunk file. */
static struct {
	char t_Tag[8];
	struct DosEnvec t_Env;
} sysenv = {
	{ 'E', '9', 'S', 'Y', 'S', 'B', 'U', 'F' },
	{ DE_DOSTYPE, BLK / 4, 0, 0, 1, 0, 0, 0, 0, 0, 0, NBUFDEF, MEMF_ANY,
	0, 0, 0, ID_V7L_DISK }
};
static struct FileSysStartupMsg fssm = {
	WD_SLOT(0, BOOTSLOT), WDNAME, &fsenv, 0
};
static struct FileSysStartupMsg sysfssm = {
	WD_SLOT(0, SYSSLOT), WDNAME, &sysenv.t_Env, 0
};

/* List device name on fm's unit, run from seg. */
static struct DosList *
fslist(name, fm, seg)
char *name;
struct FileSysStartupMsg *fm;
APTR seg;
{
	register struct DosList *dl;

	if ((dl = MakeDosEntry(name, (LONG)DLT_DEVICE)) == NULL)
		return (NULL);
	dl->dol_misc.dol_handler.dol_Handler = fsname;
	dl->dol_misc.dol_handler.dol_StackSize = 1280;
	dl->dol_misc.dol_handler.dol_Priority = 10;
	dl->dol_misc.dol_handler.dol_Startup = (LONG)fm;
	dl->dol_misc.dol_handler.dol_SegList = (BPTR)seg;
	AddDosEntry(dl);
	return (dl);
}

/* The romtag's init, as (0, seglist, SysBase): list DH0 on the boot
 * slot and, when the system slot exists, DH1 on it, which becomes
 * SYS:.  Both run this seglist, each in its own process. */
static APTR
fsinit(zero, seg, sysbase)
APTR zero, seg, sysbase;
{
	struct IOStdReq r;
	register struct DosList *dl;

	if ((DOSBase = (struct DosLibrary *)OpenLibrary(DOSNAME, 39L)) == NULL)
		return (NULL);
	if ((dl = fslist("DH0", &fssm, seg)) == NULL)
		return (NULL);
	r.io_Message.mn_Length = sizeof(r);
	if (OpenDevice(WDNAME, WD_SLOT(0, SYSSLOT), &r, 0L) == 0) {
		CloseDevice(&r);
		if (fslist("DH1", &sysfssm, seg) != NULL)
			AssignLate("SYS", "DH1:");
	}
	return ((APTR)dl);
}

struct Resident fstag = {
	RTC_MATCHWORD, &fstag, (APTR)(&fstag + 1), 0, 39,
	NT_UNKNOWN, 0, fsname, fsid, (APTR)fsinit
};
