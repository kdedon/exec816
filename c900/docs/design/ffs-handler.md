# Amiga OFS/FFS handler (scope)

Status: on hold (2026-11-15); the cost is not yet judged worth it.
Nothing here is implemented or a contract. It sizes a port of AROS's `afs.handler` as an opt-in Exec900
filesystem for secondary partitions and Amiga hard-disk images.

## Goals and non-goals

Goals:

- Read and write OFS and FFS volumes (`DOS\0`-`DOS\3`) so files move
  between Exec900, real Amigas, WinUAE and wb900 without a host step.
- Serve a wd.device partition or a whole second drive holding an HDF.
- Opt-in: loaded and mounted only when a mount entry names it; no cost to
  a system without one.

Non-goals:

- **Replacing V7 on the boot volume.** kboot boots from V7 and COHERENT
  shares the volume, so `DH0:` stays `v7fs-handler`.
- **PFS3.** Not readable by stock Kickstart or AROS's handler, so it helps
  no exchange path; its source is larger and its licence separate. FFS is
  what every Amiga, WinUAE and AROS mount by default.
- **Floppies.** The C900 controller cannot read Amiga track format. An
  FFS-formatted C900 floppy would exchange with nothing.
- **Booting from FFS**, RDB filesystem loading (`FSHD`/`LSEG` hold 68k
  code), dircache writes (`DOS\4`/`\5`), long names (`DOS\6`/`\7`),
  multi-user (`muFS`), notification, links, `ExAll`.

## Source

AROS `rom/filesys/afs`, at wb900's AROS base commit `eb6ffb32`
(`~/git/wb900/aros/UPSTREAM.md`). No local AROS tree holds it; the sizes
below are from that commit.

| Files | Bytes | Use |
| --- | ---: | --- |
| `main.c` (packet loop), `volumes.c`, `misc.c` | 55,692 | port |
| `filehandles1.c`, `filehandles2.c`, `filehandles3.c` | 91,418 | port |
| `bitmap.c`, `validator.c` | 62,936 | port |
| `cache.c` | 17,343 | rewrite (see [Performance](#performance)) |
| `checksums.c`, `hashing.c`, `error.c`, `extstrings.c` | 4,621 | port |
| `os_aros_support.c`, `os_aros_requesters.c` | 29,560 | replace with an Exec900 layer |
| headers | about 31,000 | port |
| `afs_security.c`, `os_unix_support.*` | 20,259 | drop |

About 260 KB of C is ported. Every file says "Copyright (C) 1995-2026,
The AROS Development Team"; the licence is the AROS Public License 1.1
(MPL 1.1 based, file-level copyleft; wb900 keeps its text). Version 41.15.

**Licence placement.** APL 1.1, like MPL 1.1, is not GPL-compatible, so the
port cannot be linked with GPL Exec900 code such as `v7fs.c`. It fits
[LICENSING.md](../../LICENSING.md)'s application permission: a separately
built handler using the MIT headers, `fd/` files and `lib/` helpers, with
its files staying APL. It belongs in wb900 (which already keeps APL ports)
or its own repository, not under `c900/dos`. Reusing v7fs's cache code
needs its author to offer it under MIT; otherwise the cache is written
anew in the port.

**Dialect.** At toolchain `ca463ce` cc0 accepts prototypes, ANSI
definitions, `//` comments, declarations after statements, automatic
structure initializers and unbounded automatic arrays
(`commodore-900-toolchain/docs/PATCHES.md`; [internals](../internals.md#toolchain-facts)
still says K&R only and needs updating). What remains to convert:

- GCC `static inline` and `__attribute__((noinline|always_inline))`
  (`os.h`, `os_aros_support.h`): plain `static` functions or macros.
- `UQUAD`: used only for the byte offset in `readwriteDisk`; there is no
  64-bit type. wd.device takes sector numbers, so the offset becomes a
  `ULONG` sector ([below](#wddevice)).
- Variadic debug macros `D(bug(...))`, `DB2(...)`: defined empty.
- AROS macros: `AROS_BE2LONG`/`LONG2BE` are identities (the Z8001 is big
  endian, as FFS is); `AROS_BSTR_*` and `BADDR` become C-string and
  identity forms, as Exec900's BSTRs are C strings; `AROS_INTH1` and
  friends become an Exec900 interrupt or are removed with change
  interrupts. `IPTR`/`SIPTR` are `ULONG`/`LONG`.
- Externals over 15 characters: `tools/shortnames.py` with a map.
- Objects over 64 KB: the 64 KB bulk read buffer in `cache.c` and
  `countUsedBlocks`'s window shrink (8 KB).

**Dependencies.**

| Used by afs | On Exec900 |
| --- | --- |
| exec: memory, ports, `DoIO`/`SendIO`/`AbortIO`, `Wait`/`Signal`, lists | present |
| dos: `DateStamp`, `MakeDosEntry`, `AddDosEntry`, `RemDosEntry`, `FreeDosEntry`, `UnLockDosList` | present |
| dos: `AttemptLockDosList` | missing; implement in dos.library (small) |
| dos: `StrCpyToBstr`/`StrCpyFromBstr`, `TaggedOpenLibrary` | AROS-private; replace with `strcpy`, `OpenLibrary` |
| utility: `ToUpper` | no utility.library; a local function |
| timer.device `UNIT_VBLANK`, `TR_ADDREQUEST` (1 s flush) | present |
| input.device `IECLASS_DISKINSERTED`/`REMOVED` events | absent; drop (fixed media) |
| intuition requesters (`showError`, `showRetriableError`) | absent; log and fail with the error code; optional EasyRequest when wb900's intuition is open |

**Size estimate.** `v7fs.c` (37 KB of source) compiles to 14.4 KB of code.
At that ratio less AROS's heavier commenting and debug code, the port is
**60-90 KB of code, 3-6 KB of data**, so two or three code hunks with
`mkhunk.py --split` (each hunk 8 bytes of loader header). dos.library is
32 KB for comparison. Measure after phase 1; the validator (35 KB of
source) is the first candidate to load late or trim.

## Exec900 work

### DOS mounting

dos.library starts a handler from `dol_SegList` or, without one, loads
it from `dol_Handler` (`dos/doslist.c dstart`; done for the long-name
`DH1:`); nothing mounts a device after boot. Proposed,
following AmigaOS 2.0:

1. **`C:Mount`**, reading Amiga MountList syntax from `DEVS:MountList`
   (or one file per device in `DEVS:DOSDrivers`, names within V7's 14
   bytes): `Device = wd.device`, `Unit`, `Flags`, `FileSystem =
   L:FastFileSystem`, `DosType = 0x444F5303`, `LowCyl`, `HighCyl`,
   `Surfaces`, `BlocksPerTrack`, `Reserved`, `Buffers`, `BufMemType`,
   `MaxTransfer`, `Mask`, `StackSize`, `Priority`. It allocates the
   `FileSysStartupMsg` and `DosEnvec`, `LoadSeg`s the filesystem once
   (shared through the resident list, as Amiga's FileSystem.resource
   would), fills `dol_SegList` and `AddDosEntry`s the device. The boot
   `S:Startup-Sequence` runs it when the file exists.
2. **dos.library**: start a handler by `LoadSeg(dol_Handler)` when
   `dol_SegList` is NULL, as AmigaOS does. Done.
3. **`<dos/filehandler.h>`**: add `de_Baud`, `de_Control`,
   `de_BootBlocks` (`DE_BOOTBLOCKS` 19); afs reads `de_BootBlocks` when
   `de_TableSize >= 19`. afs takes the type from the boot block, not
   `de_DosType`; Mount still sets it for `Info` and Format.
4. **Packets.** afs answers every `ACTION_*` dos.library sends. Calls
   that would reach its extra packets are missing or unimplemented:
   `Inhibit` (`ACTION_INHIBIT`), `Format` (`ACTION_FORMAT`), `Relabel`
   (`ACTION_RENAME_DISK`), `AddBuffers` (`ACTION_MORE_CACHE`),
   `OpenFromLock`, `DupLockFromFH`, `ParentOfFH` (`ACTION_FH_FROM_LOCK`,
   `_COPY_DIR_FH`, `_PARENT_FH`) and `SetOwner`. The `ACTION_` constants
   exist in `<dos/dosextens.h>`. Inhibit, Format and Relabel are needed for
   phase 3; the rest are small packet wrappers.
5. **Format.** `C:Format DRIVE NAME [FFS] [INTL] [QUICK]`: `Inhibit`,
   `Format(drive, name, dostype)`, uninhibit. afs's `ACTION_FORMAT` writes
   the boot block, root and bitmap.

### wd.device

No wd.device change is needed for slots on drive 0. The port's I/O layer
adapts:

- **Offsets.** wd.device's `io_Offset` is a zero-based 512-byte sector in
  the unit, not trackdisk's byte offset. The layer passes
  `startblock + block` (sectors). No 64-bit offsets, TD64 or NSD: a drive
  holds at most 2^21 sectors (1 GiB), the WD2010 142.6 MB, and the boot
  ROM's drive types 10 to 42.9 MB. `NSCMD_DEVICEQUERY` gets
  `IOERR_NOCMD`, after which afs stays on 32-bit commands; verify it is
  not fatal.
- **Geometry.** `TD_GETGEOMETRY` maps to `WD_GETGEOMETRY` (12, a 12-byte
  `WDGeometry`, not `DriveGeometry`). `TD_CHANGESTATE`, `TD_PROTSTATUS`,
  `TD_MOTOR`, `TD_ADDCHANGEINT` answer as fixed, writable media; the media
  generation check uses `WD_CHANGENUM` (16).
- **Drive 1** needs wd.device work. Its raw unit (`WD_RAW(1)`, unit 32) is
  read-only and sized from drive 0's highest slot end, and its slot units
  reuse drive 0's `bi_part` extents. A whole-disk HDF on drive 1 needs a
  writable whole-drive unit and a size: from the mount file's geometry, or
  a new kboot.cfg line. The writable raw unit departs from upstream's
  read-only raw rule and needs a decision. The C emulator ignores the LUN
  (drive 1 is drive 0's image) and must learn drive 1 for tests.

### Locating a partition

A mount entry names `Unit = WD_SLOT(0, n)` (slot n is unit n + 1) and
describes the slot as `LowCyl = 0`, `Surfaces = 1`, `BlocksPerTrack = B`,
`HighCyl = blocks / B - 1`. wd.device adds the slot's `bstart` and refuses
blocks past `bcount`, so the handler never sees absolute addresses.

The FFS root block is at `(reserved + total - 1) / 2`, so `total` must be
the block count the formatter used, not the slot or file size. For an HDF
made elsewhere, copy its geometry (WinUAE's hardfile surfaces, sectors,
reserved) into the mount entry; an RDB, when present, gives it exactly.

## Partitioning

### kboot today

From `~/git/commodore-900-kboot` (0e5ee49), confirmed by c900-c0:

- No on-disk partition table. kboot builds `bi_part[]` from kboot.cfg
  lines `part <slot> <start> <blocks>` (`src/cfg.c`), scoped to the
  preceding `os`/`system` line.
- `struct bipart` is `{ bstart, bcount }`, unsigned longs, in 512-byte
  blocks (`include/bootinfo.h`). No type, ID, name or DosType field.
  `BI_NPART` is 16; an empty slot is 0/0 and refuses every block.
- `mkdisk.py` uses slots 0 and 1 (test pattern, scratch), 14 (the exec
  V7 volume) and 15 (the whole drive). Slots 2-13 are free.
- kboot never reads inside a partition, and the BIOS does not read
  partitions at all.

### Options

| Option | kboot change | Notes |
| --- | --- | --- |
| kboot.cfg extent + Exec900 mount file keyed by slot | none | Recommended. The extent comes from `part`, the type and geometry from `DEVS:MountList` |
| Amiga RDB inside a slot or at a fixed drive block | none | Invisible to kboot. Optional: lets WinUAE/Amiga HDFs carry their own geometry; a reader in `C:Mount` (RDSK/PART blocks, read-only) |
| Type field in `bipart` or kboot.cfg | bootinfo format change | Optional, needs the user's decision; no session owns kboot |

COHERENT's behaviour when probing a slot holding a foreign filesystem is
unchecked; it should touch only slots it mounts.

### kboot answers

Filled in from c900-c0's reading of kboot 0e5ee49 and the core:

1. **Type field:** none; adding one is a bootinfo/kboot.cfg format change
   for the user to decide.
2. **RDB acceptance:** kboot ignores partition contents, so an RDB is
   harmless and Exec900's choice.
3. **Second disk image on MiSTer:** the core has HD0 (OSD slot 2) and HD1
   (slot 3), separate raw 512-byte-sector images with C900 drive
   geometry (`rtl/host/disk_stream.sv`); HD1 is drive 1 on the
   controller. The BIOS and kboot boot only drive 0; `bi_part` extents
   are on the boot drive. Whether kboot can address drive 1 through a
   slot is unchecked (wd.device: see [drive 1](#wddevice)).
4. **Limits:** 16 slots, 512-byte blocks, unsigned long extents, 8 drive
   types in the core.

Still open for kboot's owner: whether a `part` line may name drive 1, and
whether kboot issues Set Drive Parameters for drive 1.

## MiSTer

- **Partition inside the C900 image (HD0).** Works today on drive 0:
  `mkdisk.py` reserves the extent and a `part` line names it. Best for
  tests and the bench (21 MB images, type 1).
- **Second image (HD1).** Built into the core (C900_MiSTer "second hard disk"; OSD index
  3, drive 1 on the WD1003 cable, its own disk stream); the three-slot
  wiring is only the `C900_DISK_DEBUG` overlay. No end-to-end test of
  drive 1 (BIOS, kboot or COHERENT reading and writing it) is on record,
  so HD1 must be checked before relying on it. The BIOS boots drive 0
  only, and kboot's `part` lines have no drive field. An HDF there is a
  plain block image: its size must suit a C900 drive type, and the mount
  entry gives its FFS geometry. Needs the wd.device drive 1 work.

## Performance

At 6 MHz (the C emulator runs about 1.5 M instructions a second) block
handling dominates:

- **Cache.** AROS's `cache.c` scans a linked list linearly, takes LRU by
  counter and allocates buffers with `AllocVec`, which are not on a
  512-byte physical boundary: wd.device would bounce every transfer one
  sector per command. The replacement follows v7fs's design: one
  allocation aligned for direct DMA, a hashed slot table, clock
  replacement, adjacent dirty blocks written in one `CMD_WRITE`, and
  read-ahead of up to 8 contiguous blocks in one `CMD_READ`.
- **Data.** FFS data blocks are raw 512 bytes, so a large `Read`/`Write`
  can DMA straight to the caller's buffer, as v7fs does; OFS blocks carry
  a 24-byte header and always go through the cache. Default to FFS.
- **Checksums.** Every header, bitmap and OFS data block sums 128 longs;
  under 1 ms a block at 6 MHz. Measure.
- **Directories.** A lookup hashes into the header's 72-entry table and
  follows one chain, typically one or two block reads. `ExNext` reads
  every entry's header: about one block per file, which a 16-buffer cache
  does not hold for large directories. No dircache: AROS's afs does not
  maintain it, so `DOS\4`/`\5` volumes mount read-only (writing would
  leave the dircache stale for real Amigas).
- **Bitmap.** Allocation reads one bitmap block at a time (4,064 blocks
  each; 72 for a 142 MB drive). The validator, run at mount when the root's
  bitmap-valid flag is clear, holds the whole bitmap in memory (blocks / 8
  bytes, 10 KB for 40 MB) and reads every header, which may take minutes
  on a full volume at 6 MHz. FFS's consistency after a reset is this
  validator, weaker than v7fs's ordered writes.

**Memory**, per mounted volume: code 60-90 KB (shared), cache 16 x 512
bytes plus about 1 KB, the validator's bitmap while it runs, 8 KB of
read-ahead, and small lock and handle records. The handler stack comes
from the `MEMF_STACK` pool (27-59 KB in all): AROS's stack depth is
unknown, so start at 4 KB and measure with `C:StackUse`. Kernel
stack-segment reservations change by 0 bytes; the cost is the per-volume
handler stack.

**Names.** FFS keeps 30 bytes and a 79-byte comment; V7 keeps 14 and no
comment. `fib_FileName` (108) and the shell cope. Copying from FFS to the
boot volume cuts names to 14, so two names may collide; `C:Copy` should
report that instead of overwriting. Protection bits (HSPARWED) survive on
FFS and are mostly lost on V7. `DEVS:`, `LIBS:` and `L:` stay on V7, so
the 14-byte rule for disk libraries is unchanged.

## Tooling and tests

- **Host.** `amitools`' `xdftool` (Python; GPL-2.0, to be confirmed) is
  not installed locally. Used as an external development tool, its
  licence does not touch Exec900. Proposed: `tools/affs.py` (MIT, standard
  library, as `cohfs.py`): format, put, get, list and `--check`
  (checksums, bitmap against reachable blocks, hash chains), and a
  `mkdisk.py --ffs=SLOT:BLOCKS[:PATH=FILE...]` option. Host tests compare
  its images with `xdftool` when present, else skip.
- **Emulator.** An image with an FFS slot on drive 0; tests that mount,
  list, type and compare checksums (read-only), then write, rename,
  delete and `affs.py --check` the image (read-write), and a reset during
  writes followed by validation (as `fscrash`). `fsbench` on FFS against
  V7.
- **Round trip.** An FFS (`DOS\3`) HDF made by `xdftool` and one by WinUAE
  under AROS or Kickstart (`~/git/WinUAE`), mounted read-only on Exec900;
  then files written by Exec900 read back in WinUAE and checked there.
- **MiSTer.** HD0 partition first; HD1 after the drive 1 work.

## Effort

| Phase | Work | Estimate |
| --- | --- | --- |
| 0 | `affs.py`, `mkdisk.py --ffs`, host tests | 3-4 days |
| 1 Read-only | Exec900 I/O layer, requesters and input stubs, dialect fixes, `--split` build, a static mount entry, read packets, emulator read test | 1.5-2 weeks |
| 2 Read-write | DMA-aligned cache, write packets, flush timer, validator, crash test, round trip with WinUAE | 1.5-2 weeks |
| 3 Tools | `C:Mount`, dos.library `Inhibit`/`Format`/`Relabel`/`AddBuffers`/`AttemptLockDosList`, `C:Format`, `C:Relabel`, optional RDB reader, drive 1 | 1-2 weeks |

## Risks

- Code size beyond 90 KB, or cc0/cc1 defects on a large foreign source;
  defects go to the toolchain with regressions, not workarounds.
- Licence: keeping APL and GPL apart, and the cache's provenance.
- Writing a dircache or long-name volume corrupts it for real Amigas;
  refuse writes to `DOS\4`-`\7`.
- A wrong geometry puts the root elsewhere: afs reports not-a-DOS-disk, or
  a format writes over the wrong blocks.
- Validator time and memory on large volumes; handler stack depth.
- COHERENT probing an FFS slot; HD1 built in the core but not tested end to end.

## Open questions

1. Where does the port live: wb900, a new repository, or an APL
   directory here?
2. May v7fs's cache code be offered under MIT for reuse?
3. Mount file format: Amiga MountList, DOSDrivers files, or both? Should
   dos.library load `dol_Handler` itself?
4. Should wd.device get a writable whole-drive unit for drive 1, and where
   does its size come from?
5. Is a kboot type field wanted at all, given the mount file?
6. Is an RDB reader worth phase 3, or are partition HDFs without one
   enough?
7. Is HD1 wired end to end in the C900 core, and does kboot issue drive
   parameters for drive 1?
8. Is read-only `DOS\4`/`\5` (dircache) access enough?
