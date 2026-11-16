# C900 platform contract

[Exec900 documentation](README.md)

This page is the contract between Exec900 and the Commodore 900: what the
kernel expects from kboot and the machine, which resources it owns, and what
is not supported. [Internals](internals.md) explains the design behind it.
The Exec API contracts are upstream's [reference pages](../../docs/reference/README.md);
this page and internals record where the C900 build departs from them.

## Boot handoff

kboot loads `exec900.sys` and jumps to `start` with:

- segmented system mode, interrupts off;
- text at logical segment 0x30 (physical 0x080000) and data at 0x31,
  directly after the text rounded up to 1 KB, with bss zeroed. The entry
  code maps the stack segment itself, its RAM from bss (the linker's
  `end`) rounded up to 256 bytes, before any C runs;
- ROM segments 0 and 1 still mapped. Segment 1 offset 0 points at the ROM
  configuration block, which supplies the CPU clock type and the fallback
  RAM range. `mach_mem()` copies both, then makes segments 0 and 1
  CPU-inhibit; nothing calls or reads the ROM after that.
- other descriptors kboot used for staging (0x0A, 0x0B, 0x0D, 0x22–0x24,
  0x2C–0x2E); `mach_mem()` reprograms or inhibits every one.

The disk controller's command block and sector buffer occupy physical
0x080000–0x0803FF, so the first 0x400 text bytes are a pad. The text
segment is read-only, so the kernel maps logical `CMDSEG` (0x06) over the
same bytes, writable, with its limit at `CMDLEN`. Drivers write the hard-disk
block at `CMD_HD`, the floppy block at `CMD_FD` and the 512-byte-aligned
bounce and parameter buffer at `CMD_BUF`; an access past `CMDLEN` traps.

The kernel carries a version 5 `bootinfo` block (kboot's
`include/bootinfo.h`) and the boot entry asks for the handoff. Exec900 reads
only the memory map `bi_ram`, and only from a block with `bi_src ==
BI_SRC_KBOOT`, the expected version and length and a zero checksum. Without
a valid map it falls back to the ROM's RAM range, which may include
framebuffer segments. `wd.device` exposes the partition slots `bi_part`
from the same validated block, and `serial.device` opens only the lines
`bi_serial` reports (line 0 alone without a valid block). The console
follows `bi_console` ([Console](#console)). Swap and boot flags are
ignored.

## Memory map

The machine has one Z8010 MMU. Its 64 descriptors translate logical
segments 0x00–0x3F; a segment 0x40–0x7F is never translated, raises no trap
and reaches nothing (reads `0xFFFF`, writes lost). At most 4 MB is mapped at
once, while the RAM card can fit up to 16 MB, so the logical space is built
around physical numbers rather than packed.

| Logical segment | Physical | Use |
| --- | --- | --- |
| 0x00 | — | CPU-inhibit: a NULL or segment-0 access takes the segment trap |
| 0x01 | 0x3F | HR bitmap second half, mapped by the HR backend's probe once it finds the card; otherwise inhibited |
| 0x02 | any 1 KB-aligned base | Window 0, Task level (`kwinmap(0, …)`) |
| 0x03 | any 1 KB-aligned base | Window 1, interrupt level (`kwinmap(1, …)`) |
| 0x04–0x05, 0x07–0x09 | RAM without an identity segment | Relocation slots, filled in RAM order: phys 0x30 and 0x31, then RAM above 0x3F. 0x09 is a slot only when the heap starts above page 0x09; unused slots inhibited |
| 0x06 | phys 0x080000, limit 0x400 | Disk command area (`CMDSEG`): writable alias of the text pad |
| 0x09–0x3D | same number | Identity heap from the end of the stack segment up, wherever a bootinfo run has RAM; otherwise inhibited |
| 0x30 | page 0x08 | Kernel text and PSA (read-only, from kboot) |
| 0x31 | after text | Kernel data and bss |
| 0x37 | 0x37 | Reserved: LR text RAM (identity), mapped while the LR card is the console, otherwise inhibited |
| 0x3E | 0x3E | HR bitmap first half (identity), mapped by the HR probe; inhibited without a card; identity heap instead when kboot reports it as RAM (no HR board) |
| 0x3F | after bss, 256-byte aligned, ending 1 KB-aligned | Stack segment (`SS`): offsets `kstklow` up, down-growing; 63 KB (`kstklow` 0x0400–0x0700) by default, down to 32 KB (0x8000–0x8300), [chosen at boot](#stack-segment-size) |

- **Kernel memory.** The image and the stack segment are one physical
  block from 0x080000: text, data and bss, then the stack segment's RAM
  from the next 256-byte boundary to the [segment size](#stack-segment-size)
  past the 1 KB boundary at or below it (62.25 to 63 KB by default, down
  to 31.25 to 32 KB). Nothing else is reserved: the heap starts
  where the stack segment ends, so each 1 KB the image grows or shrinks
  is 1 KB of allocatable memory, and the 256-byte steps within it come
  out of the stack region. `tools/loutsize.py` prints the layout of the
  32 KB segment at link time; the default image puts the 63 KB
  segment's RAM at 0x08F500–0x09EFFF (the 32 KB one at
  0x08F500–0x0973FF).
- **Identity heap.** Every page of a bootinfo run below 0x40 and past the
  stack segment is mapped at logical = physical and becomes one
  `MEMF_PUBLIC|MEMF_UPPER` MemHeader, spanning the whole 64 KB from
  offset 0. The page the stack segment ends in is mapped the same way and
  is heap from that end up, from a nonzero, 1 KB-aligned offset, first in
  the memory list, so small allocations use it before they split a whole
  page. That first region holds its own MemHeader at its start; every
  later MemHeader, the stack region's included, is allocated from it at
  boot, so whole pages are free from offset 0 for
  [blocks over 64 KB](#memory-allocation). A heap pointer's segment number is its
  physical segment, which DMA (the PDMAC, later the LANCE) needs because
  those masters bypass the MMU. Pages 0x37 and 0x3F are never heap
  (framebuffer or ROM stack RAM; kboot already excludes them).
- **Relocated heap.** RAM without a usable identity segment goes to the
  next free relocation slot and is still heap; its segment is not its
  physical number. That is physical 0x30 and 0x31 (the kernel's logical
  segments) on a 3 MB or larger machine, then the first pages above 0x3F.
- **Physical-only pool.** RAM left when the relocation slots run out
  (above physical 0x3F on a machine with more than 4 MB) is not mapped.
  It is counted in a
  kernel-private pool of 64 KB pages: `kallocphys()` returns a free physical
  segment number or −1, `kfreephys(pg)` returns one. Pool pages are reached
  only through a window. Boot prints the heap, relocated and pool page
  counts and the partial page's bytes; no RAM is dropped.
- **Address translation.** `kphys(p)` returns the 24-bit physical address
  of a logical pointer, from a table of every descriptor the kernel set
  (heap, relocated, kernel text and data read back from the MMU, stack,
  windows), or `0xFFFFFFFF` for an unmapped or untranslated segment. DMA
  setup uses it for any pointer, including stack and relocated ones.
- **Windows.** `kwinmap(win, phys)` maps window `win` (0 or 1) at the 1 KB
  base below `phys` with a 64 KB limit and returns the pointer to `phys`;
  `kwinunmap(win)` inhibits it again. A window is one global descriptor, not
  per-Task state, and the kernel does not arbitrate it: window 0 belongs to
  Task-level code, which holds `Forbid` from map to last access; window 1
  belongs to interrupt code, which maps and uses it with VI off and never
  across a return. Future users are the RAM disk and the disk cache.
- **Driver mappings.** `kmapseg(seg, page, attr)` programs one descriptor
  (`page` = phys >> 8, 1 KB-aligned) and records it for `kphys`, with VI off
  across the multi-cycle descriptor load. Video drivers map the reserved LR
  and HR slots with it; nothing maps them until a driver finds the card.
  No other descriptor is free for a driver: one that needs to reach
  physical memory uses a window.
  The console's probe does so in `con_init`, before `mach_mem()`, which
  keeps those slots' descriptors; the ROM's configuration is copied
  before the probe, since HRSEG1 is segment 1.
- **Inhibit.** Every descriptor without a mapping, segments 0 and 1 included,
  is CPU-inhibit (attribute 0x04), so a stray access takes the segment
  trap (trap 2, [traps](#traps)). Accesses to segments 0x40–0x7F cannot be caught.
- Kernel text is one 64 KB segment, data and bss another; the build
  refuses an image that overflows either. The stack segment may therefore
  end as high as 0x0A7FFF.
- **Physical page boundaries.** The stack segment is the one mapping
  whose RAM can lie across a physical page boundary (a window
  can too). `wd.device` ends each DMA command at the boundary; any other
  MMU-bypassing master given a stack-segment address must do the same.
- The ROM's state block at the top of the highest RAM page is not kept: the
  kernel reads the ROM configuration once, before inhibiting segment 1, and
  never calls the ROM, so that page is ordinary heap or pool memory.
- A MemHeader manages one segment: `mh_Upper` is an exclusive end
  offset (up to 0x10000) within `mh_Lower`'s segment. Only `AllocMem`
  blocks over 64 KB cross segments ([Memory allocation](#memory-allocation)).
- Mapped segments are system read/write with a 64 KB limit, except the
  down-growing stack segment ([budget](#stack-segment-budget)) (kernel text is
  read-only). No other segment protection is used.
- Emulator limits: `--ram` fits RAM in every segment of the range, including
  0x37 and 0x3E (holes on the real RAM card), and the ROM's memory test fails
  ("RAM ERROR") once segment 0x7F or above is fitted, so emulated runs reach
  at most `--ram=08-7E` (8 MB). Pool behaviour above that is untested.

## Memory allocation

The calls follow Amiga Exec V39 and upstream's
[memory contract](../../docs/reference/memory.md), with these differences.

- **Blocks over 64 KB.** `AllocMem`, `AllocVec` and `AllocAbs` take any
  size up to 4 MB. A block over 64 KB starts at offset 0 of a segment and
  runs over whole free pages into the start of the next: consecutive
  identity-mapped heap pages with the required attributes, so the block is
  physically contiguous and DMA can use it as one range. Relocation slots
  never join a run. The first such run from the lowest segment is used
  (from the highest with `MEMF_REVERSE`).
  `MEMF_LINEAR` is accepted and changes nothing: every such block is
  linear. A block of 64 KB or less never crosses a segment.
- **Stepping pointers.** C pointer arithmetic changes only the offset
  word, so code walking a block over 64 KB steps with `PtrStep(p, d)`,
  which returns `p` moved by `d` bytes with the carry or borrow applied
  to the segment number. It is a C900 call, not an Amiga one. Within one
  segment ordinary pointer arithmetic is still correct.
- **Freeing.** `FreeMem` frees any part of an allocated block, as on the
  Amiga, also across segments; the range is rounded out to 8 bytes.
  `MEMF_CLEAR` clears the whole block.
- **`AvailMem(MEMF_LARGEST)`** counts runs as `AllocMem` places them, so
  `AllocMem` of the result succeeds and 8 bytes more fails.
- **Attributes.** Every region is `MEMF_PUBLIC|MEMF_FAST|MEMF_LOCAL|
  MEMF_24BITDMA|MEMF_KICK` (`MEMF_24BITDMA` is upstream's `MEMF_DMA24`)
  plus `MEMF_UPPER` for the heap or `MEMF_BANK0` for the stack region, as
  `TypeOfMem` reports: all RAM is fast RAM below 16 MB that the CPU and
  DMA both reach and that exists from boot. There is no chip RAM, so
  `MEMF_CHIP` requests fail and `AvailMem(MEMF_CHIP)` is 0, as on an Amiga
  without chip RAM.
- **Stack region.** The stack segment's RAM is one MemHeader of the
  lowest priority (`ln_Pri` −10), typed `MEMF_BANK0` (`MEMF_STACK`), as
  an Amiga's chip RAM is a typed region that `MEMF_ANY` reaches last.
  `MEMF_STACK` requests come only from it, first fit from its bottom.
  A request with neither `MEMF_STACK` nor `MEMF_UPPER` takes heap first;
  only when the heap and then the [low-memory
  handlers](#memory-allocation) fail does it take the region, from the
  top of its highest fitting chunk (as `MEMF_REVERSE`), so stacks and
  such blocks meet only when the region is full. Exception: such a
  request of 128 bytes or less without `MEMF_REVERSE` takes the region's top first, while the
  region keeps 20 KB free. Long-lived small nodes in a free heap page
  otherwise split the page runs that [blocks over 64 KB](#memory-allocation)
  need: on 1 MB, one 296-byte node in page 0x16 made wb900's asl fail to
  allocate a 640x400x4 bitmap (128,000 bytes) with pages 0x15 and 0x17
  free. Departure: Amiga Exec places every block by first fit. The limits
  are tuned on wb900's 1 MB tests (asl, wbmulti, edit): 128 bytes passes
  all three with 8 to 32 KB kept, and 96 to 160 bytes with 24 KB; 36 KB
  or more, or 64 bytes, fail asl's bitmap, and 256 bytes passes only with
  24 to 32 KB. `MEMF_UPPER` and
  `MEMF_24BITDMA` exclude the region: its RAM can cross a physical 64 KB
  page, and DMA blocks are kept inside one. `AvailMem(MEMF_24BITDMA)`
  still counts it. A block there is segment 0x3F, valid in every Task and in
  interrupt code; `TypeOfMem` reports `MEMF_BANK0` for it, and its RAM
  can lie across a physical page boundary
  ([memory map](#memory-map)). `AvailMem` counts the region with every
  request but `MEMF_UPPER` (it is the whole of `MEMF_STACK`), so
  `AvailMem(MEMF_ANY)` and `MEMF_TOTAL` count it once and
  `MEMF_LARGEST` may be a chunk of it. Pools, `AllocVec` and
  `AllocEntry` take it the same way through `AllocMem`; `AllocAbs`
  claims any free range. Departure: the Amiga calls the handlers only
  after every region failed; here they run before the stack region,
  because data left there while the heap held expungeable libraries
  would stay when the next stack allocation fails, and handlers free
  only heap. Before, the region was a stack-only pool whose unused
  bytes no other allocation could use.
- **Flags.** `MEMF_REVERSE` takes the highest fitting space: the memory
  list is searched from its end (the highest pages) and a block comes from
  the top of the highest chunk that fits. `MEMF_LARGEST` and
  `MEMF_TOTAL` are ignored by `AllocMem`, as on the Amiga; `AvailMem` with both `MEMF_LARGEST` and `MEMF_TOTAL` returns the
  total. Upstream departures kept: bits that name no `MEMF_` flag still
  make a call fail (the Amiga ignores them), as do `MEMF_BANK0` with
  `MEMF_UPPER` or `MEMF_LINEAR`, which are upstream flags with no Amiga
  meaning.
- **Low-memory handlers.** `AddMemHandler(is)` and `RemMemHandler(is)`
  are Amiga V39's. When an `AllocMem` (or `AllocVec`, `AllocEntry`)
  fails, each handler, by `ln_Pri`, is called under `Forbid` in the
  allocating Task as `LONG is_Code(struct MemHandlerData *, is_Data,
  SysBase)` (Amiga's A0, A1 and A6) and the allocation retried:
  `MEM_DID_NOTHING` moves to the next handler without a retry,
  `MEM_ALL_DONE` retries, then moves on, `MEM_TRY_AGAIN` retries and, if
  that fails, calls the same handler again with `MEMHF_RECYCLE` in
  `memh_Flags`. A handler must not `Wait` or allocate memory: an
  allocation from inside a handler, with `MEMF_NO_EXPUNGE`, or before
  the first Task skips the handlers. dos.library's ramlib handler
  expunges the [disk libraries](#disk-libraries-and-devices).
- **`AllocAbs(size, p)`** claims the free range at `p` (rounded out to 8
  bytes) and returns `p`. A range over a segment end must lie in
  identity-mapped pages.
- **Pools.** `CreatePool`, `DeletePool`, `AllocPooled` and `FreePooled`
  are Amiga V39's: `CreatePool` fails when the threshold exceeds the
  puddle size; a request over the threshold gets its own `AllocMem` block
  (over 64 KB if needed); a puddle that becomes empty is freed; a pool
  with `MEMF_CLEAR` clears every block; `DeletePool` frees all blocks.
  The caller serialises calls on one pool, unless it is created with
  `MEMF_SEM_PROTECTED`: then `AllocPooled`, `FreePooled` and
  `DeletePool` hold a SignalSemaphore in the pool header, so several
  Tasks can share the pool. Other pools pay one word test per call.
  Extension: `MEMF_SEM_PROTECTED` is not in the Amiga NDK (3.2's
  `exec/memory.h` stops at `MEMF_TOTAL`, bit 19); it is adopted from AROS
  for source compatibility, because wb900's AROS libraries
  (layers.library's shared ClipRect pool) need it. Its value here is
  bit 22, not AROS's bit 20, which is `MEMF_LINEAR`; binaries built for
  AROS are not supported anyway. `AllocMem` rejects the flag.
  Extension: `AllocVecPooled(pool, size)` and `FreeVecPooled(pool, mem)`
  are adopted from AROS, MorphOS and OS4 for source compatibility
  (intuition.library's input handler actions need them).
  `AllocVecPooled` is `AllocPooled` of `size` + 8, with that total in
  the first long and the result 8 bytes in, so it keeps the granule;
  it returns NULL for size 0 or a NULL pool. `FreeVecPooled` is
  `FreePooled` of the stored total and does nothing for NULL; the pool
  must be the one allocated from (AROS ignores it). Each takes a
  protected pool's semaphore once. Departure: a puddle is one
  MemHeader, which manages one segment, so a puddle size over 65,504
  bytes is cut to that, and a threshold over it with it; larger requests
  then take the own-block path.
- **`CopyMem` and `CopyMemQuick`** copy across segment ends; both use the
  word move when source, destination and length are even, and bytes
  otherwise. Overlapping ranges are not supported, as on the Amiga.

## Stack-segment budget

Every compiled frame address is relocated against segment 0x3F, so every
Task stack and every trap frame lives in that one segment. Only offsets
`kstklow` to 0xFFFF have RAM; `kstklow` is the segment's 1 KB step
(0x8000 for 32 KB, down to 0x0400 for 63 KB) plus the image's end modulo
1 KB rounded up to 256 bytes. The
descriptor grows down, with its limit on the page below `kstklow`. A
write to that page takes the segment trap (write warning), and any
access lower takes it as a length violation, so a stack that runs off
the pool's low end faults instead of writing over the image. It is the
C900 counterpart of the 65816
[bank-zero budget](../../docs/reference/platform.md#bank-zero-memory-budget)
and is conserved the same way: report changes to these reservations for
implementation slices, including zero, with per-Task costs separate.

| Offsets | Bytes | Reservation |
| --- | ---: | --- |
| below `kstklow` | 0 | No RAM; the page below traps on write, the rest on any access |
| `kstklow` to 0xEDFF | 59,136–59,904 (63 KB segment, default); 27,392–28,160 (32 KB) | Stack region (`MEMF_STACK`): `CreateTask` and caller-supplied stacks from the bottom; at boot, the 512-byte interrupt stack; ordinary blocks from the top once the heap is exhausted ([stack region](#memory-allocation)). Its MemHeader is in the heap |
| 0xEE00–0xEFFF | 512 | Idle Task stack |
| 0xF000–0xFFFF | 4,096 | Root (boot) Task stack; initial SP 0xFFFE |

These are the default offsets. A smaller or larger
[root stack](#root-stack-size) moves the idle stack and the pool's top
with its low end; the pool gains or loses the difference.

Per-Task costs, inside each Task's own stack:

| Item | Bytes |
| --- | ---: |
| Trap frame (r0–r13, identifier, FCW, PC) | 36 |
| Interrupt headroom below the lowest frame, trap frame included | 256 |
| New-Task setup: finalizer address and initial frame | 40 |
| Guard word at `tc_SPLower` | 2 |

A Task stack must cover its own deepest frame plus the 256-byte interrupt
headroom, because interrupts and the reschedule trap push their frame on the
interrupted stack. Interrupt handlers themselves run on the interrupt
stack, so an interrupt costs a Task stack only the 36-byte frame; the
headroom now covers the reschedule path (`kswitch`) and the EPU's
context switch, which runs in the extended-instruction trap on the Task's
stack: about 100 bytes below the frame, 220 on a Task's first EPU use,
which allocates its save area. `AddTask` and `CreateTask` refuse a stack too small for
the setup frame plus that headroom, or with an odd bound. Pool allocations round to the 8-byte
allocator granule.

Overflow below the pool's low end traps at once. Inside the pool, Exec
probes each Task's stack as Amiga Exec does: `kswitch` checks the saved
SP against `tc_SPLower`–`tc_SPUpper` and the guard word `AddTask` writes
at `tc_SPLower` (the root and idle Tasks get one at boot), and the tick
checks the interrupted SP. A failure raises the recoverable alert
`AN_StackProbe` (`0x0100000E`) and rewrites a broken guard. The alert
is not repeated while `LastAlert` already holds it for the same Task:
the console line takes longer than a tick at 9600 baud, so an SP left out
of bounds would otherwise alert on every tick and the Task would never
run again. Amiga Exec repeats it on every check. The probe sees an
overrun only after it happened: the block below the stack may already be
overwritten.

An overflowing command is not ended as a command failure; as on AmigaOS
it is the alert, then whatever the overrun broke. Ending only the
command is unsafe for two reasons. The probe runs at a switch or a tick,
after the overrun, and nothing records how far below `tc_SPLower` it
wrote: the block below may be another Task's stack, an ordinary block
that spilled into the region, or a free chunk of the pool, so neither that memory nor the pool's free list can be trusted
again, and freeing the command's stack into it could spread the damage.
And the overrun usually happens inside a library (intuition, layers,
graphics) or in `Wait`, holding semaphores, a `Forbid` or half-updated
lists; unwinding the command to `RunCommand` from there would leave them
so. Only an overrun caught in the command's own code, outside
`Forbid`/`Disable` and within memory the command owns, could be unwound,
and reserving that memory below each command stack only enlarges the
stack. What is detected: `AN_StackProbe` names the Task (`Shell Process`
for a command); an overrun past the pool's low end takes the segment trap
at once, which the default `tc_TrapCode` makes a dead end. A command
that needs more than the default runs after `Stack`.

### Stack-segment size

The segment's size is chosen once, at boot, in 1 KB steps from 32 to
63 KB. The kernel image carries a word for it, `sc_kb` in `kstkcfg`
(after the 8-byte tag `E9STACKK` in its data, big-endian), which
`mkdisk.py --stack=KB` sets on the disk's copy. 0, the default, means
63 KB on every machine; other values below 32 mean 32 and values above
63 mean 63. Boot prints `mach: stack segment N KB from KSTKLOW`.

The maximum costs no memory: ordinary allocations use the
[stack region](#memory-allocation) once the heap is full, so every KB of
segment that stacks leave free is still allocatable. Before, the region
was stack-only and a 1 MB machine defaulted to 32 KB to keep the heap.
A smaller size makes that memory heap (`MEMF_UPPER`) instead, which
the handlers can reclaim before ordinary blocks reach the region, and
leaves fewer bytes to stacks; `tests/tailmem` uses `--stack=32` for the
large partial heap page. Measured on 1 MB with the bare kernel (`tests/stkmix`:
the DOS handlers, the Boot process, the Workbench's Tasks with an open
menu and a Shell, 21,248 bytes, then 4,000-byte command stacks):

| Segment | Stack region | Heap | Commands that fit | Stack bytes left |
| --- | ---: | ---: | ---: | ---: |
| 32 KB (former 1 MB default) | 27,392 | 951,064 | 1 | 2,144 |
| 48 KB (wb900's former `--stack=48`) | 43,776 | 934,672 | 5 | 2,528 |
| 63 KB (default) | 59,136 | 919,320 | 9 | 1,888 |

With 63 KB the 978,456 bytes of heap and region are all allocatable,
less what stacks hold; with the former stack-only pool the region's
free bytes were lost to everything else.

### Root stack size

The root Task runs on the boot stack, from `sc_root` in `kstkcfg` to
0xFFFE; the idle stack's 512 bytes lie directly below `sc_root` and the
pool ends there. `sc_root` follows `sc_kb` and the 4-byte tag `ROOT`,
big-endian, and holds 0x10000 less the size: 0xF000 (4,096 bytes) by
default. `mkdisk.py --rootstack=KB` sets it on the disk's copy, 1 to 16
KB; each KB below 4 adds 1 KB to the pool. On the default image
`--stack=45 --rootstack=1` gives the 44,288-byte pool of `--stack=48`
and leaves 3 KB more heap.
`mkdisk.py` refuses `--rootstack` for an image without the `ROOT` tag,
which keeps 4,096 bytes.

1 KB is the minimum. Boot, from `crt.s` to the root program's entry,
uses 496 bytes of the root stack on the emulator, 316 of them before the
root Task exists (measured with `--break` and `--dump` on the zeroed
stack); a DOS boot to a waiting root program uses 506 (`C:StackUse`).
With the 256-byte interrupt headroom that leaves about 250 bytes of a
1 KB stack to the root program, so a root program that does more than
start others and wait needs more. Values below 1 KB are refused rather
than measured per image. The guard word and the stack probe use
`tc_SPLower`, so they follow `sc_root`.

`crt.s` always maps the 32 KB segment. For a larger size `mach_mem()`,
before any Task exists and with VI off, moves the descriptor's base up by
the growth over the start of the heap, which must lie in the kernel's
RAM run (otherwise boot says so and keeps 32 KB): the boot stack's live
words, from SP to 0xFFFE, are copied through window 0 to the same offsets
in the new RAM and the descriptor is reloaded in the same assembly
routine (`kstkmove`). Offsets do not change, so every pointer into the
segment stays valid, and the segment's RAM still starts directly after
the image; only `kstklow` drops. The guard page and the length limit
move with it. The heap starts the growth later: on the default image
63 KB takes 31 KB of the partial heap page (35,840 bytes become 4,096)
and gives the region 59,136 free bytes instead of 27,392 once the
interrupt stack is allocated. The move costs about 70 instructions at
boot.

Boot also clears the stack segment from the idle stack's low end to the
boot stack's SP (about 4.5 KB by default, some 2,400 instructions), so the root and
idle stacks start cleared like a `CreateTask` stack. Nothing else fills
stacks: `CreateTask`, `CreateNewProc` and `RunCommand` already allocate
theirs with `MEMF_CLEAR`, as amiga.lib's `CreateTask` does.

[`C:StackUse`](#dos) reports each stack's deepest use from those zero
bytes. Amiga Exec has no call for it, and none is added.

`StackSwap(sss)` is Amiga exec's: it exchanges `stk_Lower`, `stk_Upper`
and `stk_Pointer` with the Task's bounds and stack pointer, with
interrupts off, and writes the guard word at the new `stk_Lower`, so the
probe follows the Task onto the new stack. The new stack must be
`MEMF_STACK` memory and is subject to the same headroom rule.
dos.library's `RunCommand` uses it.

The EPU adds no reservation here: a Task's EPU save area is heap memory
([EPU](#z8070-epu)).

Driver Tasks: `parallel.device` creates one with a 768-byte pool stack at
its first open and frees it after the last close; no other resident driver
has a Task, so the fixed reservations above are unchanged.

DOS adds no fixed reservation either; every DOS stack is a per-Task pool
allocation, made when the process or command starts and freed when it
ends ([DOS](#dos)):

| Stack | Bytes | Measured use (emulator) |
| --- | ---: | ---: |
| CON: handler process (`con-handler`, while running) | 1,024 | 314 (`tests/dosboot`) |
| Volume handler process (`v7fs-handler`, one per running volume) | 1,280 | 896 (`tests/fswrite`) |
| RAM: handler process (`ram-handler`, while running) | 1,024 | 413 (`tests/shellx`) |
| Shell process (`L:Shell-Seg`) | 2,048 | 1,148 (`tests/shellx`) |
| A process from `CreateNewProc` without `NP_StackSize` | 1,536 | 1,108 (the test's Boot process, which starts both handlers and the shell) |
| A command under `RunCommand` (`cli_DefaultStack`, 1,000 longwords) | 4,000 | 228 (`C:StackUse` itself) |
| Crash process (`crash`, once intuition has shown a crash) | 3,072 | `DisplayAlert` alone: 2,120 with no screen open, 1,336 on one (wb900) |
| Crash requester process (`crash requester`, while its requester is up) | 2,560 | 1,782 (wb900 `tests/crashreq`) |

The measured use is `C:StackUse`'s and includes the interrupt frames that landed on each stack,
but not the 256-byte headroom rule. The system `tests/dosboot` runs (the
three handlers, the Boot process, a shell and one command) takes 11,424
bytes of the region; with the former 1 MB default of 32 KB, 20,480 of
the 27,904 bytes stayed free between commands and 16,480 while a
command ran, 31,744 more with the 63 KB default.

The default command stack is AmigaOS's 4,000 bytes on every machine.
Intuition and gadtools programs need 2.6–3.0 KB (More, Edit), which
overflowed the former 1,600. A Workbench adds about 11 KB of stacks to
the booted system (input.device 4,096, the Workbench process 3,584,
console.device 2,048, the Workbench handler 1,536) and intuition's menu
Task 3,072 while a menu is shown: with them and a Shell, nine commands
at 4,000 bytes fit on 1 MB with the default 63 KB segment (one with the
former 32 KB, five with 48; table above); a failed stack allocation
fails the command with `ERROR_NO_FREE_STORE`, as before. These
Workbench sizes are wb900's source constants, not measured here. `Stack` changes the default per CLI, also
in `S:Startup-Sequence`, and new shells inherit it.

## Tick and time

Z8036 CIO #1 counter/timer 3 interrupts at **100 Hz** on vectored interrupt
0; the time constant is chosen from the ROM's CPU type (4 or 6 MHz). The
scheduler quantum is 4 ticks.

Upstream's tick is the 50 Hz (PAL) or 60 Hz (NTSC) vertical blank. Delays
are counted in ticks on both builds and are not rescaled: `Sleep(n)` waits
`n` ticks, 10 ms each here, so a delay copied from 65816 code runs 1.7 to
2 times faster unless converted.

`INTB_VERTB` (`<exec/interrupts.h>`) is `INTV_TICK`, so Amiga code that
adds a server with `AddIntServer(INTB_VERTB, is)` joins the tick chain.
**VERTB is the 100 Hz system tick, not a vertical blank**: no display
interrupt exists, the rate is 100 Hz rather than 50 or 60, and there is
no `INTF_VERTB` (no INTENA).

`timer.device` has the AmigaOS 3.x interface: units `UNIT_MICROHZ`,
`UNIT_VBLANK`, `UNIT_ECLOCK`, `UNIT_WAITUNTIL` and `UNIT_WAITECLOCK`,
`TR_ADDREQUEST`, `TR_GETSYSTIME` and `TR_SETSYSTIME`, and `AddTime`,
`SubTime`, `CmpTime`, `ReadEClock` and `GetSysTime` at Amiga's LVOs
([internals](internals.md#timerdevice)). Departures:

- The E-clock is C/T 3's input, 3,000,000 Hz at 6 MHz (2,000,000 at
  4 MHz), not 709,379 Hz: use the rate `ReadEClock` returns.
- Clock reads use C/T 3's current count between ticks where it reads
  back (the Go model and the core); otherwise (the C emulator) the
  system time and E-clock advance only at each tick, 10 ms.
- Every request completes at a tick, never between: `UNIT_MICROHZ`
  delays are no finer than `UNIT_VBLANK`'s, and a short delay lasts until
  the next tick (two without a readable count). No delay ends early.

The system time counts seconds since 1978-01-01 00:00:00 and starts at
the battery clock's at boot. `TR_SETSYSTIME` does not write the battery
clock; `C:Date` sets both. The battery clock (MSM58321) is
`battclock.resource`: `ReadBattClock`, `WriteBattClock` and
`ResetBattClock` in `<resources/battclock.h>`, for years 1978–2077
([internals](internals.md#battclock)).

## Console

The serial console is the motherboard SCC channel B: status port
0x0101 (bit 2 Tx empty, bit 0 Rx available), data port 0x0111. `con_init`
programs it at 9600 8N1 before the first byte, because on a video machine
the ROM leaves the channel reset. `KPutStr` and `KPrintf` are diagnostic
output, not an Exec device, and work from any context. Once serial.device
has started, a Task with VI on puts console bytes in a 256-byte queue
that the channel's Tx interrupt sends, and waits only while it is full;
interrupt level, VI off (`Disable`, panics, dead-end alerts) and early
boot send polled, after the queue, so output stays in order and a panic's
text is out before the system stops. Tasks reach the same line as
`serial.device` unit 0. While unit 0 is open it takes all console input,
and its output interleaves with the kernel's by byte.

When a valid bootinfo block's `bi_console` names a video card (LR, HR or
LC; `BI_CON_VID` and `BI_CON_ANY` try HR, then LR) and its probe passes,
the kernel maps it in `con_init`: its logical slot (LRSEG 0x37, or
HRSEG0/HRSEG1) stays mapped. Drawing is the card's display module,
`lr.display`, `hr.display` or `lc.display`, which the kernel loads from
the boot volume before the `Modules` list ([Boot modules](#boot-modules)).
Until it attaches, console bytes go to the serial line and into a 1 KB
ring; the module draws the ring first, so the screen shows the boot log
from the first kernel line, then every console byte goes to the serial
line and to the display, through the terminal emulator. Text a Task
prints while holding `Forbid` (`KPrintf`, `KPutStr`) is drawn a line at a
time: a partial line appears at the next newline or at the outermost
`Permit`. A panic or
dead-end alert after that draws on the screen, polled with VI off; one
before it, or without the module, is on serial only. Console input is the
serial line, then, with a card, the keyboard (`kbd_getc`). Without a card,
or with `bi_console` SER, the console is serial only and no module loads.
`textcon.device` unit 0 is the Task interface to the same console; its
`CD_ASKDISPLAY` and `CD_SETDISPLAY` commands connect the module
([internals](internals.md#console)). While
[framebuf.resource](#framebuffer) lends the bitmap, console output goes to
the serial line and the ring only.

The kernel's device is `textcon.device` (`TEXTCONNAME`,
`<devices/textcon.h>`; 14 characters, so it also fits a DEVS: file name).
`console.device` (`CONSOLENAME`, `<devices/console.h>`) is the name of the
Amiga window console, which Workbench loads from DEVS: and which draws in
intuition windows. Exec900 does not provide it: without Workbench,
`OpenDevice("console.device", ...)` fails. This follows Amiga, where
console.device is the window console; the text console is an Exec900
addition with its own name. `CONU_*` and `CONFLAG_*` are in
`<devices/conunit.h>` as on Amiga; `CD_ASKDISPLAY` and `CD_SETDISPLAY`
(`CMD_NONSTD` + 16 and + 17) are textcon.device's own and do not overlap
the Amiga keymap commands.

## Framebuffer

`framebuf.resource` (`<resources/framebuf.h>`, MIT; vectors from
`fd/framebuf_lib.fd`) describes the console card's bitmap and lends it to
one owner at a time. It exists when the console card has a bitmap: HR,
or LC (described, not CPU-mapped). With LR, no card or a serial console,
`OpenResource(FRAMEBUFNAME)` returns NULL. The calls take
`FrameBufBase` as their hidden first argument, Task context only, and
never wait.

| Call | LVO | Result |
| --- | ---: | --- |
| `GetFBInfo()` | −6 | `struct FBInfo *`, read-only and fixed for the boot |
| `AllocFB(name)` | −12 | NULL: the caller now owns the bitmap; else the owner's name |
| `FreeFB()` | −18 | Gives the bitmap back to textcon.device |

- **Description.** `fbi_Card` is `FBC_HR` or `FBC_LC` (textcon.device's
  `CDC_` numbers). `fbi_Modes` has one entry, the mode the card has when
  it is lent (`fbi_NumModes` 1, `fbi_Mode` 0). On HR that mode is a
  strap and cannot change. On LC it is the console's text mode, the
  mode an owner gets: the 8568 is programmable, and the modes an owner
  may set are the owner's to list (see **The owner**), not the
  resource's.

  | Card, mode | Size | Depth, format | Bytes a row | Split row, byte | `fbm_Half` | `fbm_Size` |
  | --- | --- | --- | ---: | --- | --- | ---: |
  | HR 1024x800 | 1024x800 | 1, `FBF_MONO` | 128 | 0xFFFF (none) | 0x3E000000, 0x01000000 | 0x20000 |
  | HR 1024x1024 | 1024x1024 | 1, `FBF_MONO` | 128 | 0xFFFF (none) | as above | 0x20000 |
  | HR 640x400 | 640x400 | 4, `FBF_IRGB` | 320 | 204, 256 | as above | 0x20000 |
  | LC (default) | 640x200 | 1, `FBF_MONO` | 80 | 0xFFFF (none) | NULL, NULL | 0x10000 |
  | LC, `--lcmode=400` | 640x400, interlaced | 1, `FBF_MONO` | 80 | 0xFFFF (none) | NULL, NULL | 0x10000 |

  `fbm_Size` is the bitmap memory from offset 0 (on HR the whole 128 KB,
  more than one screen in 800-line mono and colour). Offset o is at
  `FB_ADDR(m, o)`; row y starts at `FB_ADDR(m, (ULONG)y *
  fbm_BytesPerRow)`, and its first `FB_ROWSPLIT(m, y)` bytes are there,
  the rest at `fbm_Half[1]`. Pointer arithmetic stays inside one half.
  The HR mode is chosen once, at boot, and the console draws in the
  same mode. In order of precedence:
  1. The boot setting: a word after the 8-byte tag `E9HRMODE` in the
     kernel image's data (`khrcfg` in `devices/console.c`, big-endian),
     which `mkdisk.py --hrmode=800|1024|rgbi` sets on the disk's copy to
     the `CDM_` code of the JP3 jumper (1, 2, 3; `auto` and the default
     are 0). It replaces the strap read, mono/colour included; the
     card's other probes still decide whether a card is fitted.
  2. kboot's `BF_HR1024`, which turns a mono strap read into 1024 lines.
     kboot.cfg can already set it (`bflag hr1024 2` and `flags hr1024`
     in the entry), but it cannot correct a colour/mono read, so
     `--hrmode` uses the image word.
  3. The card's M/C strap (bits 1:0 of a 0x0404 word read,
     [hardware](hardware.md#hr-card)) through one table in
     `devices/console.c` (`hrstrap`). It reads 1024x1024 as 1024x800.

  A wrong setting is the operator's: the system cannot read the jumper,
  so it describes and draws the mode it was told. A bitmap owner that
  knows better (a screen-mode preference) may use the card in its own
  mode while it holds the bitmap; `GetFBInfo` keeps reporting the boot
  mode, and the console draws in it again after `FreeFB`.

  The LC mode is also chosen once, at boot: the height of the LC entry
  in the kernel image's data (`fbmodes` in `devices/console.c`, the
  entry after the one holding the 8-byte tag `E9LCMODE`), which
  `mkdisk.py --lcmode=200|400` sets on the disk's copy (default 200).
  `lc.display` reads it with `GetFBInfo` when it attaches and draws
  80x25 text in that mode: 8x8 cells (`font8x8`) at 640x200, or 8x16
  cells (`font8x16`, the HR font) at 640x400 interlaced, whose
  registers are in the [hardware reference](hardware.md#lc-interlace).
  The interlaced mode needs a monitor or scaler that takes 15.7 kHz
  interlace; the MiSTer core weaves it from C900_1611d1e on. The entry
  describes the mono bitmap of that size an owner can program; the
  system has no LC drawing path, so the owner programs the 8568 itself.
- **Ownership** follows `misc.resource`'s `AllocMiscResource`: the first
  `AllocFB` wins, and any later one, the owner's included, returns the
  owner's name (the caller's string, which must stay valid while it owns
  the bitmap). `FreeFB` while textcon.device owns it does nothing; by
  anyone but the owner it is a caller error, not checked. An owner that
  exits without `FreeFB` keeps the bitmap: no resource tracking.
- **The owner** has the bitmap and, on HR, the control port, and
  serialises its own drawing. `AllocFB` leaves the screen as it was.
  On LC the owner has the whole 8568 (ports 0x0181 and 0x0183, every
  register and all 64 KB of its RAM) and may reprogram it: bitmap or
  text, attributes, colours (R26), timing, interlace. Keeping R37 at
  0xC0 keeps the sync polarity the monitor had. `FreeFB`, and a
  crash, give the console back its own mode: `lc.display`'s `cd_Reset`
  rewrites every writable register (all but R16-R19, R30 and R31) for
  the boot's mode, interlace included, and positive syncs (R37 = 0xC0,
  the reset value's polarity), reloads its font at 0x2000 and clears the text and attribute planes,
  so the owner needs to restore nothing. It must leave the chip idle,
  every access waited for Update Ready and no INTR pending (a status
  read that finds it ready), as the [LC display](internals.md#lc-display)
  backend does, or the NVI stays asserted. On
  HR the owner may write `FBHR_BLANK` (0x03), `FBHR_DIM` (0x07) or
  `FBHR_FULL` (0x0F) to `FBHR_CTL` (0x0404) with `outb` at any time;
  textcon.device writes `FBHR_FULL` when it gets the card back. Every
  I/O write to an even port of the card's block (0x0400 included) loads
  the same latch, so never write the mouse ports. The mouse stays
  gameport.device's, whoever owns the bitmap.
- **While lent** console output (`KPutStr`, `KPrintf`, `CMD_WRITE`,
  `Alert`'s console line) goes to the serial line and into the 1 KB console
  ring, emptied by `AllocFB`, so its latest kilobyte is kept;
  textcon.device does not touch the bitmap or the control port. On LC
  the only access is the status read (0x0181) every NVI starts with,
  which does not disturb the 8568's register index.
  `FreeFB` clears the card (control 0x0F and 128 KB on HR, about 0.2 s),
  resets the terminal emulator and draws the ring, oldest first, from its
  first whole line after a wrap, before returning.
- **Crashes.** `kpanic`, a dead-end `Alert` and a trap without a handler
  take the bitmap back without asking, with VI off: clear, the ring,
  then the message, drawn polled. The owner never runs again.

Departures from Amiga, and why: Amiga has no framebuffer resource;
graphics.library owns the display and console.device draws through it.
Here the console draws on the card itself, so the bitmap needs an owner
protocol, and the info call stands in for the hardware knowledge
graphics.library has built in. Ownership is `misc.resource`'s model, the
nearest Amiga one for a unit two drivers share; it is a separate resource
because it also describes the bitmap. A recoverable alert is shown by
intuition.library's `DisplayAlert` when it is loaded
([Alerts](#alerts)); otherwise it is console text only.

## Keyboard

`keyboard.device` unit 0 reads the C900 keyboard on CIO U70 port A
([internals](internals.md#keyboarddevice)). Its events, `KBD_READMATRIX`
bits and the console's keymap use Amiga raw key codes
(`<devices/rawkeycodes.h>`, under the AROS names), as AmigaOS
keyboard.device does: release sets `IECODE_UP_PREFIX`, the qualifier keys
are 0x60-0x64. The keyboard's own codes (PC XT set 1,
[hardware](hardware.md#keyboard)) map as follows:

| Keys | Keyboard codes | Raw codes |
| --- | --- | --- |
| `` ` ``, 1-0, `-`, `=`, `\` | 0x29, 0x02-0x0D, 0x2B | 0x00, 0x01-0x0C, 0x0D |
| Q-P, `[`, `]` | 0x10-0x1B | 0x10-0x1B |
| A-L, `;`, `'` | 0x1E-0x28 | 0x20-0x2A |
| Z-M, `,`, `.`, `/` | 0x2C-0x35 | 0x31-0x3A |
| Space, Backspace, Tab, Return, Esc, Del | 0x39, 0x0E, 0x0F, 0x1C, 0x01, 0x55 | 0x40-0x42, 0x44-0x46 |
| Keypad 0, 1-3, 4-6, 7-9, `.` | 0x52, 0x4F-0x51, 0x4B-0x4D, 0x47-0x49, 0x59 | 0x0F, 0x1D-0x1F, 0x2D-0x2F, 0x3D-0x3F, 0x3C |
| Keypad Enter, `-`, `/`, `*`, `+` | 0x5E, 0x4A, 0x5A, 0x37, 0x4E | 0x43, 0x4A, 0x5C-0x5E |
| Up, Down, Right, Left | 0x5F, 0x62, 0x61, 0x60 | 0x4C-0x4F |
| F1-F10, Help, F12 | 0x3B-0x44, 0x54, 0x56 | 0x50-0x59, 0x5F, 0x6F |
| Left Shift, Right Shift, Caps Lock, Ctrl, Alt | 0x2A, 0x36, 0x3A, 0x1D, 0x38 | 0x60-0x64 (Alt is `LALT`) |

Eight keys have no Amiga counterpart. They take codes of keys the C900
lacks, which the usa keymap leaves without characters, named in
`<devices/keyboard.h>`:

| Key | Keyboard code | Raw code | Name | The code elsewhere |
| --- | --- | --- | --- | --- |
| Keypad 00 | 0x53 | 0x0E | `RAWKEY_KP_00` | on no Amiga keyboard |
| Keypad CE | 0x58 | 0x1C | `RAWKEY_KP_CE` | on no Amiga keyboard |
| W1 | 0x5B | 0x2B | `RAWKEY_W1` | the international key beside Return |
| W2 | 0x5C | 0x2C | `RAWKEY_W2` | on no Amiga keyboard |
| W3 | 0x5D | 0x3B | `RAWKEY_W3` | on no Amiga keyboard |
| F13 | 0x57 | 0x6B | `RAWKEY_F13` | AROS Scroll Lock |
| F14 | 0x45 | 0x6C | `RAWKEY_F14` | AROS Print Screen |
| F15 | 0x46 | 0x6D | `RAWKEY_F15` | AROS Num Lock |

Keypad 00 and CE sit beside the keypad's codes and carry
`IEQUALIFIER_NUMERICPAD`, so a keymap can give them characters; the
others are function keys. 0x68-0x6A are left alone because they are the
mouse buttons' codes. The NDK has no `RAWKEY_` names;
`<devices/rawkeycodes.h>` has AROS's names and values exactly, so it and
AROS's copy can both be included.

The console converts the key presses with a fixed US keymap, as Amiga
console.device converts raw keys with the default keymap (usa). Its
input is `kbd_getc()` (`kernel.h`): the next character or −1, never
waiting, callable with VI off, from interrupt code and before the device
or interrupts are set up (`kbd_getraw()` then programs port A and polls
it). Events and the console's key presses are buffered separately, so a
key reaches both a `KBD_READEVENT` reader and the console. The driver
owns port A, PC2, PC3, vector 0x08 and bit 1 of the system latch 0x0205;
`ksetlatch(clr, set)` changes that write-only latch's bits for every
user.

`KBD_READMATRIX` is a polled read of the keys held now: it completes
in `BeginIO` (quick under `DoIO`, nothing replied, no `Wait`), from any
Task with its own binding (opens are not exclusive), while another Task
such as input.device holds the device with `KBD_READEVENT` pending. The
matrix changes as the interrupt and the tick server take bytes, so it
is current only while interrupts run: under `Disable` it stays as it
was. Return (0x44, byte 8 bit 4) and Esc (0x45, byte 8 bit 5) are set
while held, as every key is.

## Mouse

`gameport.device` unit 0 is the mouse
([internals](internals.md#gameportdevice)). It is its own boot module,
`gameport.dev`, with two backends:

- **HR.** When [framebuf.resource](#framebuffer) reports an HR card (the
  HR card is the console), the kernel loads it with `hr.display`. It
  polls ports 0x0400 and 0x0402 from a tick server while its controller
  type is `GPCT_MOUSE`, whoever owns the bitmap; nothing else reads
  them.
- **Serial.** Otherwise (LR, LC, no card), it loads only when the boot
  volume's `Modules` lists `gameport.dev`, and reads a Mouse Systems
  mouse (1200 8N1, 5-byte packets) on SCC U74 channel A, the
  motherboard's second serial port. It opens `serial.device` unit 1
  exclusively at load and keeps it, so serial.device refuses unit 1
  (`SerErr_DevBusy`) while it is loaded; the console stays on channel B.
  On the MiSTer core the mouse is the OSD option "Serial mouse: U74 A",
  which software cannot read: list `gameport.dev` when it is on. With
  the HR card the core keeps the option off.

Both give the same events: relative motion right and down positive,
left, middle and right buttons. The serial mouse gives an event per
half packet, the HR mouse at most one per tick. It has no pointer: a
client draws one, for example from a Task an `INTV_TICK` server
signals.

**Polled buttons (Exec900 extension).** `ULONG GetGPButtons()`
(`<devices/gameport.h>`, `<lvo/gameport_lib.h>` from
`fd/gameport_lib.fd`) is gameport.device's one base function, at LVO
−42, the first vector after `AbortIO`, as timer.device's functions
follow its own. It takes `GamePortBase` as its hidden first argument:
the program defines `struct Device *GamePortBase` and sets it from
`io_Device` after `OpenDevice`, or from `FindName(&SysBase->DeviceList,
GAMEPORTNAME)` under `Forbid`. It returns the buttons held now as
`IEQUALIFIER_LEFTBUTTON`, `IEQUALIFIER_MIDBUTTON` and
`IEQUALIFIER_RBUTTON`, 0 for none: HR reads port 0x0400's button bits
at the call, the serial backend returns the reader Task's last decoded
packet. Any Task may call it, without a unit of its own, while another
Task holds the unit with reads pending, and whatever the controller
type; it never waits, replies nothing and changes no device state, so
it serves code that cannot wait for events, such as intuition's
`DisplayAlert` running on input.device's Task. Amiga intuition read the
buttons from CIA-A and POTGO directly; on the C900 the ports and serial
unit 1 are gameport.device's, so the device reads them for callers.
It also returns under `Disable`, but there the serial value stays as
it was: the reader needs interrupts. The NDK's gameport.device has no
base functions.

## Interrupts and traps

The kernel installs its own Program Status Area; every entry runs with
FCW 0xC000 (segmented, system, interrupts off).

| PSA entry | Use |
| --- | --- |
| System call | `sc $0`: reschedule (`kswitch`); any other code is a Task trap |
| Every vectored interrupt (even vectors) | `kintr`: the number's handler or server chain; unclaimed ones are counted and dismissed |
| Non-vectored interrupt | `kintr` with `INTV_NVI`; every one first reads 0x0181 to release the LC card's 8568 |
| Extended instruction | The EPU's lazy context switch ([EPU](#z8070-epu)); otherwise a Task trap |
| Privileged instruction, segment trap | Task trap ([traps](#traps)) |
| NMI | The default trap code: a dead-end alert |

Vector 0 is the CIO #1 C/T 3 tick, a server chain the scheduler and
`timer.device` are on. Odd vectors are unsupported. Tasks run with VI and
NVI enabled (FCW 0xD800). Handlers do not nest. Other I/O ports used: Z8010 MMU
0x01FC/0x0FFC/0x08FC, CIO #1 registers, the 8568 status 0x0181 (read), and
0x0FFE for the emulator stop (`0xC900`), which hardware ignores. Vector
0x80 is the disk completion, `wd.device`'s handler; its doorbell is port
0x0500 (word writes: 1 starts, 0 resets IEO). Vectors 0x10–0x3E are the
SCCs' status-in-vector interrupts (U74 base 0x10, LR U31 0x20, U36 0x30),
`serial.device`'s handlers on the chips with a present line. Vector 0x08
is CIO U70 port A, `keyboard.device`'s handler. Vector 0x70 is CIO U66
port B (the printer's /ACK), `parallel.device`'s handler; `kparinit` resets
and programs U66 (ports 0x0080–0x00FF), which belongs to `parallel.device`
and `beep.device`. Vector 0x88 is the LANCE card, a server of
`lance.device` when that module is loaded and finds the card; it owns
ports 0x0700–0x0714.

## Traps

A trap calls the current Task's `tc_TrapCode`, as on the Amiga, with the
trap number of the nearest 68000 vector (`<exec/tasks.h>`):

| Z8001 trap | Number | 68000 counterpart |
| --- | ---: | --- |
| Segment trap (Z8010 violation: inhibited segment, limit, write warning) | 2 `TRAP_SEGMENT` | bus error |
| Privileged instruction | 8 `TRAP_PRIV` | privilege violation |
| Extended instruction, with no EPU fitted or from interrupt code | 11 `TRAP_EPA` | line F (coprocessor) |
| `sc n`, n = 1–255 | 32 + n `TRAP_SC` | `TRAP #n` |
| NMI | 31 `TRAP_NMI` | level 7 autovector; never a Task trap |

`AddTask` sets a NULL `tc_TrapCode` to `SysBase->TaskTrapCode`, as on
the Amiga. The kernel's default prints `exec900: trap N pc SEG:OFF fcw F`, then
raises the dead-end alert `AT_DeadEnd | N`. dos.library's init replaces
`TaskTrapCode` with a handler that holds a crashed Task and shows the
crash requester ([Task crashes](#task-crashes)). `AddTask` copies the
default when the Task is added, so the root Task and every Task added
before dos.library's init keep the kernel's. `tc_TrapData` is for the
handler; Exec does not touch it.

The handler is a C function, `code(ULONG trapnum, struct TrapFrame
*frame)`, called on the trapping stack below the frame with VI and NVI
off. The frame is the 36-byte trap frame: r0–r13, the identifier the CPU
pushed (the instruction for `sc`), FCW, and the segmented PC. The handler
may change any of it. Returning resumes the frame with IRET; a handler that
must not resume does not return (`RemTask(NULL)`, `Alert`). Its frames
come out of the trapping stack, below the 36-byte trap frame, so a Task
that installs one needs that room beyond the interrupt headroom.

Departures, all forced by the CPU or the build: the 68000 handler is
entered with the trap number pushed above an exception frame and resumes
with `addq #4,sp; rte`, while MWC has no register convention, so the
handler is a C call and returning is the resume. The Z8001 saves the PC
of the instruction after the trapping one (a segment trap completes the
instruction with the access suppressed); nothing restarts an
instruction. There is no divide trap: `div` by zero sets the overflow
flag. Supervisor mode on the 68000 holds off Task switches; here VI and
NVI stay off in the handler for the same effect. A trap in an interrupt
handler or in `kswitch` also goes to the current Task's handler, as on
the Amiga.

## Z8070 EPU

The Z8070 floating-point unit (the APU, a MiSTer core option) is
supported as Amiga Exec supports the 68881.

- **Detection.** Boot sets `AFF_68881` (bit 4) in `SysBase->AttnFlags`
  when an EPU answers, and prints `exec900: Z8070 EPU`. The bit means
  "an FPU is fitted"; the Amiga's other `AttnFlags` bits are never set.
  The probe sets FCW EPA, reads SC (all ones from an empty socket) and
  writes and reads back two USER rounding-mode patterns, then leaves USER
  and FFLAGS 0. SC keeps its reset value: no EPU interrupts, no overlap.
- **Context.** Each Task has its own EPU state: F0–F7 (extended),
  FFLAGS and USER, as each Amiga Task has its own 68881 frame. Only the
  Task that last used the EPU (the owner) runs with FCW EPA set; any
  other Task's extended instruction takes the extended-instruction trap,
  and Exec saves the owner's state, clears EPA in its saved frame, loads
  the trapping Task's state (all zero on its first use) and re-executes
  the instruction with EPA set. A Task that never uses the EPU costs
  nothing at a switch; one that does pays one save and load when it
  next uses the EPU after another Task has. The save area (88 bytes) is
  a `MEMF_PUBLIC` MemList named `EPU` on `tc_MemEntry`, made at the first
  use, so `RemTask` frees it. When it cannot be allocated the instruction
  is a Task trap (11).
- **Without an EPU** every extended instruction is trap 11 through
  `tc_TrapCode`; there is no emulator.
- **Rules.** Interrupt code must not use the EPU: its extended
  instructions are Task trap 11 of the interrupted Task (an Amiga
  interrupt that uses the 68881 must save it itself). A `tc_TrapCode`
  handler must not `Wait`. Extended instructions must not name R14 or R15
  as their CPU register: the trap's restart relies on the second word not
  looking like a first word to the EPU. SC is not per Task.
- **Speed.** 6 MHz only: the core's EPU has a known fault above it.

### Math libraries

`mathieeedoubbas.library` and `mathieeedoubtrans.library` are Amiga's,
at Amiga's LVOs (`fd/mathieeedoubbas_lib.fd`,
`fd/mathieeedoubtrans_lib.fd`; `<lvo/mathieeedoubbas_lib.h>`,
`<lvo/mathieeedoubtrans_lib.h>`), boot modules `ieeedoubbas` and
`ieeedoubtran` (V7 names hold 14 bytes). Each function takes the base as
its hidden first argument; doubles go by value and come back in rq0.

- **mathieeedoubbas** V39: `IEEEDPFix`, `Flt`, `Cmp`, `Tst`, `Abs`, `Neg`,
  `Add`, `Sub`, `Mul`, `Div`, `Floor`, `Ceil`. With an EPU, init points
  Fix, Flt, Add, Sub, Mul, Div, Floor and Ceil at EPU code, as the Amiga
  library picks its 68881 code at init; the rest are CPU code either way.
  `Fix` truncates and gives 0x80000000 out of range. `Cmp` and `Tst`
  treat −0 as +0 and do not order NaNs.
- **mathieeedoubtrans** V39: `IEEEDPAtan`, `Sin`, `Cos`, `Tan`, `Sincos`,
  `Sinh`, `Cosh`, `Tanh`, `Exp`, `Log`, `Pow`, `Sqrt`, `Tieee`, `Fieee`,
  `Asin`, `Acos`, `Log10`, on the toolchain's libm. `IEEEDPPow(exp, arg)`
  is arg to the power exp. `Tieee` and `Fieee` carry the single as a
  LONG. With an EPU `Sqrt` is the EPU's square root, and the rest run
  libm on the C runtime's EPU arithmetic; without one, `Sqrt` of a
  negative number is 0.
- **Scratch.** F0 is not preserved across a call, as FP0 and FP1 are not
  on the Amiga; FFLAGS accumulates the operations' flags.
- **Precision.** Both paths are IEEE round to nearest, ties to even, for
  add, subtract, multiply and divide. libm's accuracy is libm's.

## Alerts

`Alert(num)` prints `exec900: alert NNNNNNNN task NAME` on the console
and records the number and Task in `SysBase->LastAlert[0]` and `[1]`,
as Amiga Exec does. With `AT_DeadEnd` set it then stops the
system; otherwise it returns. There is no reboot.

A recoverable alert is shown through intuition.library, as Amiga Exec
shows it, once dos.library is running: dos.library patches the `Alert`
vector at init, next to ramlib's vectors. After the console line and
`LastAlert`, the patch calls intuition's `DisplayAlert(0, text, 40)` when
all of these hold:

- `num` has no `AT_DeadEnd` (a dead-end alert never returns to it);
- the caller is a Task outside `Forbid` and `Disable` (its
  `tc_TDNestCnt` and `tc_IDNestCnt` both negative) running on its own
  stack, between `tc_SPLower` and `tc_SPUpper`, so not an interrupt
  server or handler, which run on the interrupt stack;
- intuition.library is on `SysBase->LibList`. It is opened with exec's
  own `OpenLibrary`, never loaded from disk, and closed after the call.

Alerts are shown one at a time. While one is shown, another Task's alert
waits for its turn on a `SignalSemaphore` dos.library owns, in arrival
order, not by priority, as `ObtainSemaphore` queues, then is shown. An
alert waits only once all the conditions above hold, so one in `Forbid`,
in `Disable`, from an interrupt or with no intuition.library never
waits. Three callers never wait, and their alert is console text only
while another is shown: the Task already showing one (an alert raised
inside `DisplayAlert`), the ramlib process and the `input.device` Task,
which the display may itself wait for. Unsupported: any other Task that
the display waits for (a handler intuition loads from, for example)
raising a recoverable alert while one is shown stops both Tasks.

The serialisation is dos.library's, not intuition's. `DisplayAlert`'s
contract does not promise it, and AROS's `DisplayAlert`, a system
requester, takes no lock; AmigaOS 3.x intuition's source was not
available to check. Classic Amiga intuition takes over the display and
polls the mouse buttons itself, depending on no other Task. Exec's alerts
reach intuition only through the patch, so the patch serialises its own
calls, which also keeps a second alert from waiting inside intuition.

The text is Amiga Exec's, in `DisplayAlert`'s format (a 2-byte x, a y
byte, the string, a continuation byte):
`Recoverable Alert. ` at (38, 15), `Press left mouse button to continue.`
at (234, 15) and `Guru Meditation #NNNNNNNN.TTTTTTTT` (the number and
the Task, in lower-case hex) at (142, 30). The alert number passed is 0,
`RECOVERY_ALERT`, and `DisplayAlert`'s answer is ignored, as Amiga Exec
ignores it for every alert but a CPU exception. `TimedDisplayAlert` is
not used. The display runs on the caller's stack, so `DisplayAlert`'s
stack use adds to the caller's.

`AN_BadLVO`, which the kernel raises from the calling Task when it calls
an unimplemented vector, goes through the `Alert` vector, so it is shown
too. The other alerts the kernel raises itself call it directly, not
through the vector, and stay console text: `AN_StackProbe` (at a switch
or in the tick), `AN_SemCorrupt` and `AN_LibChkSum` (under `Forbid`).
Without dos.library, or with no intuition.library loaded, every alert is
console text only.

Departure from Amiga, and why: Amiga Exec's own `Alert` opens
intuition.library. Here dos.library holds that code, as it holds ramlib,
because kernel text is 4 bytes short of a 1 KB page boundary and the
code (about 500 bytes of Z8001 code and 140 of strings) would cost a
whole heap page. The behaviour seen through the vector is Amiga's;
dos.library is always present wherever intuition.library can be loaded.

## Task crashes

dos.library's trap handler (`SysBase->TaskTrapCode` from its init on)
holds a Task whose trap came in Task context, as AmigaOS 2.0+ holds one
behind its "Software Failure" requester. It holds the Task when all of
these hold:

- the trap is not NMI (NMI never reaches a Task's handler anyway);
- the frame lies within the Task's `tc_SPLower`..`tc_SPUpper`, so the
  trap did not come from an interrupt server or handler, which run on
  the interrupt stack, nor from a stack that has run off its low end
  (an overrun past the pool's low end stays a dead end);
- the Task is outside `Forbid` and `Disable` and the trapping FCW has VI
  and NVI on.

Any other trap gets the kernel's default: the trap line and the
dead-end alert. A Task that sets its own `tc_TrapCode` is not affected.

A held Task leaves trap state with interrupts on and never runs its own
code again: it prints

```
exec900: trap N pc SEG:OFF fcw F
exec900: task NAME alert 8000000N held
```

(`N` the trap number in decimal, then the alert `AT_DeadEnd | N` in
lower-case hex), then waits on no signal, `Wait(0)`, for good. It keeps
its memory, stack, open files, locks, held semaphores and pending I/O;
nothing frees them, and a Task waiting for any of them waits too. The
rest of the system keeps running. When less than 768 bytes are left
below the trap frame (a trap deep in a call chain), the held Task runs
from the top of its stack instead, overwriting what is there (an I/O
request or message on it that is still in use included); a Task
needs a stack of at least 768 bytes to be held safely.

With intuition.library on the library list (it is never loaded for a
crash), a crash process (`crash`, a 3,072-byte stack, started on the first
such crash and kept) shows the crash, one at a time, taking its turn on
the alert semaphore ([Alerts](#alerts)), unless the held Task owns it.
It starts a requester process (`crash requester`, a 2,560-byte stack) that
calls intuition's `BuildEasyRequestArgs(NULL, es, 0, &num)`: the default
public screen, title `Software Failure`, text `Program failed (error
#%08lx).\nWait for disk activity to finish.`, gadgets `Suspend|Reboot`,
then `SysReqHandler(w, NULL, TRUE)` until it answers 0 or more, then
`FreeSysRequest(w)`. A result of 1 from `BuildEasyRequestArgs` counts as
Suspend. The requester never runs on the held Task's stack, which an
overflow may have used up.

The held Task may own a lock that opening the window waits for
(graphics, layers or intuition's). When `BuildEasyRequestArgs` has not
returned within 2 seconds (timer.device, `UNIT_VBLANK`), or returns
NULL, the crash process abandons the requester and shows the crash with
`DisplayAlert(0, text, 40)` instead: `Software Failure.   Left mouse
button: Suspend.   Right: Reboot.` at (38, 15) and `Program failed
(error #NNNNNNNN).  Wait for disk activity to finish.` at (38, 30).
An abandoned requester process that gets its window later closes it
unanswered and ends; one that never returns keeps its stack. A crash of
the `input.device` Task, which the requester needs, goes to
`DisplayAlert` directly; so does every crash when timer.device cannot be
opened.

- **Suspend** (the requester's left gadget, `DisplayAlert`'s left
  button): the Task stays held.
- **Reboot**: the crash process raises the dead-end alert
  `AT_DeadEnd | N` (console line `exec900: alert 8000000N task crash`)
  and the system stops. Exec900 has no `ColdReboot`; the stop is its
  counterpart.

Without intuition.library the Task is held with the two console lines
only. A crash of the crash process itself leaves its queue to the next
one, started on the next crash; a crash of the requester process ends
its requester as one that did not open.

The privileged-instruction trap never reaches the handler alone: only
normal mode raises it, and every segment is system-only, so the
instruction fetch also takes the segment trap, at the PSA entry with
interrupts off, which is the dead end.

Unsupported: removing a held Task (`RemTask`) and freeing what it holds;
no reaper exists.

Departures from Amiga, and why: AmigaOS 2.0+ builds the requester in
exec's default trap code, on the crashed Task's stack, and without
intuition (or from supervisor state) shows the dead-end guru and resets.
Here dos.library holds the code, as it holds the alert display, because
kernel text is 4 bytes short of a 1 KB page boundary. The requester runs
in its own process because a crashed Task may have no stack left, and
behind a 2-second watchdog because the held Task may own a lock the
window needs; neither failure can happen in the same way on the Amiga's
supervisor stack. Without intuition the Task is held rather than the
system stopped, which keeps a console-only system running.

## Headers

`include/` follows the AmigaOS 3.x (NDK 3.9) layout: each structure,
constant and macro is in the header Amiga code includes for it, and each
header includes what its NDK counterpart includes.

- **exec/**: `types.h`, `nodes.h`, `lists.h`, `ports.h`, `tasks.h`,
  `libraries.h`, `devices.h`, `io.h`, `memory.h`, `semaphores.h`,
  `interrupts.h`, `resident.h`, `execbase.h`, `alerts.h`, `errors.h` and
  `initializers.h`. `exec/exec.h` includes them all and declares the
  calls.
- **Types.** `types.h` has the V40 typedefs, with `STRPTR` as
  `UBYTE *`, `BOOL` as `short`, and `CONST_STRPTR` and `CONST_APTR`.
  It has no `IPTR`. Exec900 sources declare strings `char *`, as the fd
  files do.
- **Generated parts.** `tools/genabi.py` writes the records and constants
  that come from the upstream ABI into `build/c900/include/exec/abi/`:
  `struct Task` and the `TS_` states (`tasks.h`), `struct MsgPort`,
  `struct Message` and `PA_` (`ports.h`), `struct MemChunk`, `struct
  MemHeader` and `MEMF_` (`memory.h`), `struct IORequest`, `struct
  IOStdReq` and `CMD_` (`io.h`), `IOERR_` (`errors.h`) and the upstream
  `NT_` types (`nodes.h`). The Amiga header includes its part, so a
  program needs both include directories and includes only the Amiga
  name. `<exec/abi.h>` includes every Amiga header that has a part.
- **Departures.** `devices.h` also includes `io.h`, for amiga.lib's
  `BeginIO` macro. `lists.h` adds `IsMinListEmpty`, `NewList` and
  `NewMinList`. `alerts.h` has no 68000 `ACPU_` codes.
- **Other directories.** `dos/`, `devices/`, `resources/` and `utility/`
  hold the Amiga headers for what Exec900 implements, plus
  `dos/doshunks.h` and `devices/rawkeycodes.h` (AROS's names and values,
  [Keyboard](#keyboard)). As AROS does, `devices/inputevent.h` adds
  `IECLASS_NEWMOUSE` (0x16, so `IECLASS_MAX` is 0x16), and
  `devices/newmouse.h` has the NewMouse codes `NM_WHEEL_UP`/`_DOWN`/`_LEFT`/
  `_RIGHT` (0x7A-0x7D) and `NM_BUTTON_FOURTH` (0x7E), the
  `RAWKEY_NM_*` values. AROS's own tree has only the class and the
  `RAWKEY_NM_*` names; the `NM_*` names are the NewMouse header's. No
  Exec900 driver sends these events. `utility/tagitem.h` and `utility/hooks.h` are the
  NDK's; there is no utility.library.

## Libraries and SysBase

exec.library, the resident devices, `battclock.resource` and
`framebuf.resource` are Amiga libraries: a `struct Library` base (`<exec/libraries.h>`) preceded by a
jump table, with `MakeLibrary`, `MakeFunctions`, `InitStruct`,
`AddLibrary`, `OpenLibrary`, `OldOpenLibrary`, `CloseLibrary`,
`RemLibrary`, `SetFunction`, `SumLibrary`, `InitResident`,
`FindResident`, `InitCode`, `AddResource`, `OpenResource` and
`RemResource` as in Amiga Exec V39.

- **Vectors.** Vector n sits at base − 6n and is the Z8001 instruction
  `jp addr` with a long segmented address: the words `0x5E08`,
  `0x8000 | seg << 8` and the offset (as-z8001 encodes `jp sym` so). It is
  the size of a 68000 `jmp abs.l`, so LVO offsets are Amiga's. Kernel
  text is read-only, so every table is in the data segment, in bss or in
  `AllocMem` memory. `SetFunction` rewrites the three words with
  interrupts off (`Disable`), then sums the table.
- **Calls.** A call is an indirect `call` to base + LVO. MWC has no
  register convention, so the base, which a 68000 library receives in A6,
  is a hidden first argument: `fn(base, args...)`. exec.library's
  functions are the exception: they take no base, because `SysBase` is
  a global and the kernel calls the same functions directly, and a
  hidden argument would shift every parameter. A function installed with
  `SetFunction` over an exec.library vector therefore takes the exec
  function's own arguments. Library `Open` is `(lib, version)`, `Close`
  and `Expunge` are `(lib)` and return a segment list (NULL for
  residents), and `libInit` and a non-autoinit `rt_Init` are called as
  `(lib or 0, segList, SysBase)`, Amiga's D0, A0 and A6. Device vectors
  are in `<exec/devices.h>`.
- **Macros.** `tools/genlvo.py` generates `<lvo/exec_lib.h>`,
  `<lvo/battclock_lib.h>`, `<lvo/framebuf_lib.h>` and `<lvo/timer_lib.h>`
  from `fd/*.fd`: `LVO<name>` constants and a
  casting macro per function (`AllocMem(n, f)` calls SysBase − 198).
  Programs call through the table; kernel sources, compiled with
  `EXEC_KERNEL`, call the `x` functions directly, so `SetFunction` on an
  exec.library vector changes what programs see, not the kernel.
- **exec.library** has Amiga V39's 131 vectors (−6 to −786), then the
  Exec900 extensions `ExecVersion` (−792), `Yield`, `Sleep`,
  `CreateTask`, `IntStray`, a reserved vector (−822), `TermIO`, `KPutStr`,
  `PtrStep`, `InternalLoadSeg`, `InternalUnLoadSeg` and the test-only
  `TestHooks` (−858), five reserved vectors (−864 to −888), and
  `AllocVecPooled` (−894) and `FreeVecPooled` (−900) at their AROS,
  MorphOS and OS4 offsets: 150 vectors, `lib_NegSize` 900 with no
  padding. `lib_Version` is
  39. A V39 function Exec900 does not provide
  is `call kunimpl`: it raises the recoverable alert `AN_BadLVO |
  −LVO` (`0x01008000 | 6n`) through the `Alert` vector and returns 0. These are `Supervisor`, the
  private vectors except `RawPutChar`, `Debug`, `SetSR`, `SuperState`,
  `UserState`, `Cause`, `SetExcept`, `AllocTrap`, `FreeTrap`, `GetCC`,
  `SumKickData`, `AddMemList`, `CacheClearU`, `CacheClearE`,
  `CacheControl`, `ColdReboot` and `ObtainQuickVector`; the reserved −822, once an Exec900 `BeginIO`, and
  −864 to −888 are the same stub. `NewList`, `NewMinList` and `KPrintf` are linked
  functions, not vectors, as Amiga's amiga.lib and debug.lib calls are:
  the kernel has its own, programs link `lib/amiga.c`'s;
  `BeginIO` is a macro in `<exec/devices.h>` that calls the device's −30
  vector, as amiga.lib's does.
- **Formatting.** `RawDoFmt(fmt, data, proc, pdata)` is Amiga's: each
  output character, then a NUL, goes to `proc(c, pdata)`, and it returns
  the address after the last argument used. Conversions are `KPrintf`'s
  ([Console](#console)); `%d`, `%u`, `%x` and `%c` take a word, as on the
  Amiga, which is also MWC's `int`. `%p` (a long, 8 hex digits) is an
  extension; `%b` (BSTR) is not supported. The private `RawPutChar`
  (−516) writes one console byte and drops NUL, as debug.lib's
  `KPutChar` expects; a program's `KPrintf` is `RawDoFmt` into it under
  `Forbid`.
- **DMA addresses.** `CachePreDMA(addr, &len, flags)` returns the
  physical address of `addr` (the Z8001 has no cache to flush) and cuts
  `len` at the end of `addr`'s 64 KB segment, past which physical memory
  need not continue; an unmapped address gives `0xFFFFFFFF` and a length
  of 0. `CachePostDMA` does nothing.
- **TestHooks** returns the kernel internals the white-box tests use
  ([internals](internals.md#tests)). It is not an API: its table changes
  with the kernel, and programs other than tests must not call it.
- **SysBase.** Address 4 is in segment 0, which is CPU-inhibited so NULL
  accesses trap. `SysBase` is instead the first long of the kernel data
  segment, logical `0x31000000`; it points at exec.library's base, which
  follows its jump table in data.
- **ExecBase** (`<exec/execbase.h>`) holds, after `LibNode`, the Amiga
  fields programs read: `TaskTrapCode`, `MemList`, `ResourceList`,
  `DeviceList`, `LibList`, `PortList`, `TaskReady`, `TaskWait` and
  `LastAlert[4]`, in Amiga order but not at Amiga offsets, `AttnFlags`
  ([EPU](#z8070-epu)) and the Exec900 extension `ex_TickCount`, the
  100 Hz ticks since the tick started. Read the library, device,
  resource and port lists under `Forbid` and the Task lists under
  `Disable`, as on the Amiga. Amiga's other fields (`ThisTask`,
  `IntrList`, ...) are kernel variables, not part of the base.
- **Checksums.** `SumLibrary` sums the longs of `lib_NegSize` bytes below
  the base when `LIBF_SUMUSED` is set; a mismatch without `LIBF_CHANGED`
  raises `AN_LibChkSum` (`0x01000003`), recoverable, as its Amiga value.
- **Word-offset tables.** `MakeFunctions` and `MakeLibrary` accept
  Amiga's relative form (`-1` first word), but the offsets are added to
  the offset word only: they work only when the functions and the
  displacement base share a segment. C data is in segment 0x31 and code
  in 0x30, so a C program builds absolute tables.
- **Residents** are the kernel's `ResModules` array, in initialisation
  order; `InitCode(RTF_COLDSTART, 0)` starts them at boot. Nothing scans
  memory for `RTC_MATCHWORD`; [boot modules](#boot-modules) are scanned
  when loaded.

## Program loading

Programs, libraries and modules are Amiga hunk files. `tools/mkhunk.py`
makes them from MWC objects linked with `ld -r` and `platform/progabs.o`.

- **Calls.** `InternalLoadSeg(fh, table, funcarray, stack)` and
  `InternalUnLoadSeg(seglist, freefunc)` are dos.library V36's, as
  exec.library extension vectors: exec must load dos.library itself, so
  the loader cannot live in it. DOS will build `LoadSeg` and `UnLoadSeg`
  on them. `funcarray` is `{ read, alloc, free }`: `read(fh, buf, len)`
  returns the bytes read or −1, `alloc(size, flags)` and
  `free(mem, size)` are `AllocMem`'s and `FreeMem`'s shapes. MWC has no
  register convention, so no DOSBase is passed. `InternalLoadSeg` returns
  the seglist or NULL; `InternalUnLoadSeg` frees every segment with
  `free` and returns −1 (DOSTRUE).
- **Hunks.** `HUNK_HEADER` (no resident library names), then per hunk
  `HUNK_CODE`, `HUNK_DATA` or `HUNK_BSS`, optional `HUNK_RELOC32` and
  `HUNK_END`. The header's table size is the hunk count; its first and
  last numbers are read and ignored, so the first hunk is 0. Bits 30 and
  31 of a hunk size ask for `MEMF_CHIP` and `MEMF_FAST`, as on the Amiga;
  both set (extended flags) is not supported and a chip request fails.
  The high word of a hunk type is ignored. Overlays, `table`, `stack`,
  `HUNK_SYMBOL`, `HUNK_DEBUG`, `HUNK_NAME` and the other hunk types are
  unsupported: the load fails and frees what it allocated. A hunk is at
  most 65,520 bytes; a file may have any number of hunks.
- **Segments.** Each hunk is one `alloc(size + 8, MEMF_PUBLIC |
  MEMF_CLEAR | chip/fast)` block: the block size, the link, then the
  hunk, so data past the file's longs and all of bss read as zero. The
  seglist points at the first link; each link holds the next segment's
  link address or NULL. This is Amiga's layout with native pointers
  instead of BPTRs. A program's entry is the first hunk, `seglist + 4`;
  mkhunk always makes a code hunk first.
- **Relocation.** A `HUNK_RELOC32` offset names a long in the current
  hunk holding a segmented pointer: bit 31 (set in an instruction's long
  address operand, clear in a C pointer), zero segment bits and the offset
  within the target hunk. The loader replaces bits 30–16 with the target
  hunk's segment and adds its base offset to the low word modulo 65,536.
  A block of at most 64 KB never crosses a segment, so the sum needs no
  carry, and a negative addend wraps as Z8001 offset arithmetic does. An
  odd offset or one past the hunk fails the load.
- **Executable heap.** Heap descriptors are system read/write
  (`MMU_SYS`); the Z8010 has no no-execute attribute, so code runs from
  any heap block.
- **Linking.** A program needs no kernel symbols: it reaches exec through
  `SysBase`, which `progabs.s` defines at the fixed address `0x31000000`
  (the first long of the kernel's data segment), and the
  `<lvo/exec_lib.h>` macros. A program links, in order, `lib/startup.s`,
  its own objects, the program library and `progabs.o`, and is converted
  with `mkhunk.py --entry=start`. `progabs.s` also defines `SS`, the stack
  segment cc2 relocates frame addresses against. mkhunk refuses an
  unresolved external, a byte, word or high-word relocation, a
  PC-relative reference into another hunk and uninitialised text.
- **Over 64 KB.** A Z8001 address cannot cross a 64 KB segment and
  `ld -r` makes text, data and bss of at most 64 KB each, so a larger
  program, library or module is linked in groups, each its own hunks, as
  an Amiga linker emits several code hunks:

  ```sh
  mkhunk.py --split --ld=$LD [--entry=start] OUT obj... [lib.a...] progabs.o
  ```

  Objects are taken in order into groups whose text, data and bss each
  fit in 65,520 bytes; each group is linked with `ld -r` and every object
  holding only absolute symbols (`progabs.o`). Archives are linked as one
  more group, with `-u` for each symbol the groups leave undefined. Each
  group's text is a code hunk, the first group's hunk 0; data and bss are
  packed into as few data and bss hunks as fit. mkhunk resolves the
  references between groups and writes them as `HUNK_RELOC32` entries, so
  calls and data references cross hunks as within one. Equivalently,
  link each group yourself (`ld -r -o gN.r objs... progabs.o`) and run
  `mkhunk.py OUT g0.r g1.r ...`.
  - Cross-group references work because MWC reaches every external
    symbol through a long segmented address (`CALL`, `LD`, `LDA` and C
    pointers). PC-relative forms (`JR`, `CALR`, `DJNZ`, `LDAR`) reach
    only within one object; mkhunk refuses one that would leave its hunk,
    as from hand-written assembly calling another group.
  - One object's text, data or bss must fit in 65,520 bytes, so one
    function or one data object must too. A single archive group must
    also fit; otherwise list the members as objects.
  - External names keep 15 characters. `ld` merges two names that agree
    in those across the whole link; mkhunk refuses a name defined in two
    groups but cannot see a reference that merged with another name. Run
    `tools/symcheck.py` over all of the program's sources together.
  - `--keep` keeps the group files, `OUT.N.r`.
- **Startup.** `lib/startup.s` puts `start` at the start of the code
  hunk; it jumps to `main`, whose `int` result is the program's. The
  loader has already relocated the hunks and cleared bss, and `SysBase`
  is absolute, so it sets up nothing; there is no argument string. The
  root program, boot modules and handlers start this way.
- **DOS programs** (commands, the shell) start with `lib/dstartup.s`
  instead: dos.library calls the first hunk as `LONG start(char *args,
  LONG len)` ([DOS](#dos)), and `lib/dosstart.c` opens dos.library into
  `DOSBase`, calls `main(args, len)` and returns its result.
- **Program library.** `lib/amiga.c` provides `NewList` and `KPrintf`
  (as amiga.lib and debug.lib do), and `platform/portio.s` `inb`, `outb`
  and `outw`; programs run in system mode, so port I/O works.
  `lib/dosstart.c` adds amiga.lib's `Printf` and `FPrintf`. Ordinary C
  programs use the [C library](#c-library) instead.

## Boot modules

DOS, its handlers and the shell are not in the kernel image; kernel text
is one 64 KB segment. They are loaded at boot as modules.

- **Boot volume.** kboot does not say which partition it loaded
  `exec900.sys` from, so the boot volume is `bi_part[14]` (`BOOTSLOT`) of
  drive 0, `wd.device` unit `WD_SLOT(0, 14)`; `kboot.cfg` must name the
  exec partition there (`part 14 136 1024` on `mkdisk.py` disks). Without
  that slot no modules load.
- **Filesystem.** The kernel reads the COHERENT V7 filesystem kboot reads:
  the root directory only, regular files through the double-indirect
  block (8.4 MB), read-only.
- **Modules file.** After the tick starts and before the root program,
  the kernel reads `Modules` from the boot volume: one file name per
  line, blank lines and lines starting with `#` skipped, only the first
  511 bytes read. Each named file is loaded with `InternalLoadSeg`, and
  every romtag in it (`RTC_MATCHWORD` followed by a pointer to itself) is
  passed to `InitResident(romtag, seglist)` in file order, whatever its
  `rt_Flags`, `rt_Pri` or `rt_Version`. A file that is missing, fails to
  load or holds no romtag prints `exec900: module NAME not loaded` and is
  freed. Loaded modules stay; `FindResident` and `InitCode` see only
  `ResModules`.
- **Display module.** Before the `Modules` list, the kernel loads the
  module for the console's card the same way: `lr.display`, `hr.display`
  or `lc.display`, then, for HR, `gameport.dev` (gameport.device; V7
  names hold 14 bytes). They are not listed in `Modules`, except
  `gameport.dev` for the serial mouse on other cards
  ([mouse](#mouse)).
- `mkdisk.py --module=FILE` puts FILE on the exec partition and lists it
  in `Modules`; `--file=FILE` adds it unlisted. The Makefile adds the
  three display modules and `gameport.dev` to every disk with `--file`.

Departures, all forced by the image size or the loader: Amiga Kickstart
modules are ROM residents found by `InitCode`, and later modules come
through `KickTagPtr`; here they are disk files loaded once at boot.

## Disk libraries and devices

Libraries and devices that are neither resident nor boot modules load
from disk on first open, as AmigaOS 2.0's ramlib does it.
dos.library's init `SetFunction`s exec.library's `OpenLibrary`,
`OldOpenLibrary`, `OpenDevice`, `CloseLibrary`, `CloseDevice`,
`RemLibrary` and `RemDevice` vectors (`dos/ramlib.c`). Programs and
modules call through the vectors; kernel code calls exec directly and
never loads from disk.

- **Loading.** When the open fails and the name's file part
  (`FilePart`) is not on `SysBase->LibList` (`DeviceList`), the file
  `LIBS:name` (`DEVS:name`), or the name as given when it has a `:` or
  `/`, is loaded with `LoadSeg`. The first romtag in its hunks
  (`RTC_MATCHWORD` followed by a pointer to itself) goes to
  `InitResident(romtag, seglist)`, and the file part is opened again.
  With `RTF_AUTOINIT` the romtag's init table makes the base and adds it
  as `rt_Type` says; without it `rt_Init` adds it itself. Names compare
  exactly, so the file's name is the library's; the V7 filesystem keeps
  14 bytes of a name, so `LIBS:intuition.library` is the file
  `Libs/intuition.libr`, and the node's name comes from the romtag.
- **Failures.** A missing or unreadable file, a file without a romtag, a
  romtag whose `rt_Name` is already listed (a boot module, say) and an
  init that returns NULL each leave nothing loaded: the seglist is
  unloaded and the open returns NULL, or `IOERR_OPENFAIL` with
  `io_Device` NULL. A failing `libInit` frees the base `MakeLibrary`
  allocated, as on the Amiga. A name already listed is never loaded, so
  a boot module and its disk copy are not both resident, and a library
  that is too old is not reloaded.
- **Who loads.** A process loads in its own context. A plain Task sends
  the request to the `ramlib` process (started on the first such
  request, priority 0, 2,048-byte stack) and waits, unless it holds
  `di_DevLock`, `di_EntryLock` or `di_DeleteLock` exclusively; then it
  loads in its own context, since ramlib could not obtain them. One
  semaphore serialises loads: concurrent opens of a name load it once,
  and a library's init may open another disk library.
- **Unloading.** A library's `Close` or `Expunge` returns its seglist
  when it has removed itself, as on the Amiga (`LIBF_DELEXP` and delayed
  expunge are the library's own). The patched close and remove calls
  unload a seglist ramlib loaded when it comes back; `RemLibrary` still
  returns it, already unloaded. ramlib's
  [low-memory handler](#memory-allocation) calls the `Expunge` of every
  library and device it loaded: a closed one goes, an open one sets
  `LIBF_DELEXP` and goes at its last close. An `Expunge` that closes
  other disk libraries (asl.library closing gadtools.library) can unload
  them during the walk, so the walk starts again after any unload.
  Seglists ramlib did not load
  (boot modules, a program's own `LoadSeg` and `InitResident`) stay the
  loader's.
- **Limits.** Opening a library not yet loaded may `Wait`, which breaks
  a `Forbid`, as on the Amiga; it is not for interrupt code. A handler
  cannot load from its own volume, and a Task holding the DosList locked
  for writing while another loads can deadlock, as on the Amiga.

Departures: a process loads in its own context instead of always
through the ramlib process (AROS's LDDemon does the same): semaphores the
caller holds then nest instead of deadlocking ramlib, and the load costs
no switch. The low-memory handler expunges only what ramlib loaded; on
Exec900 every other library is resident or a boot module, whose Expunge
frees nothing.

### Building a disk library

`tests/hunks/dlt.c` with `tests/hunks/dlt_lib.fd` is a complete
example, and the Makefile's `dlt` rules build it.

1. **fd file** (MIT), in the C900 dialect: `##base _FooBase`,
   `##bias 30`, `##public`, one line per function in vector order with
   its C argument types, return type and C name, then `##end`:

   ```
   FooAdd(a,b)(LONG,LONG) LONG @foadd
   ```

2. **genlvo.** `python3 tools/genlvo.py foo_lib.fd
   $(OUT)/include/lvo/foo_lib.h --table $(OUT)/obj/foovec.c FooFuncs fo`
   writes the callers' header (`LVOFooAdd` and a casting macro calling
   `FooBase − 30` with `FooBase` as the hidden first argument) and the
   vector array `FooFuncs`: `foopen`, `foclose`, `foexpunge`, `fonull`,
   then the fd functions (`founimpl` for one not implemented).
3. **Library source.** Every function takes the base first. A base
   struct beginning with `struct Library` keeps the seglist. `foopen(lib,
   ver)` counts `lib_OpenCnt` and clears `LIBF_DELEXP`; `foexpunge(lib)`
   sets `LIBF_DELEXP` and returns NULL while open, else `Remove`s the
   base, frees it (`lib_NegSize + lib_PosSize` from `lib − lib_NegSize`)
   and returns the seglist; `foclose(lib)` calls it at the last close
   with `LIBF_DELEXP` set. The init, `(lib, seglist, SysBase)`, sets
   `ln_Name`, `lib_IdString` and `lib_Version`, keeps the seglist and
   returns the base, or frees it and returns NULL. The romtag is
   `{ RTC_MATCHWORD, &tag, &tag + 1, RTF_AUTOINIT, version, NT_LIBRARY,
   0, name, idstring, &inittab }` with `inittab` `{ sizeof(base),
   FooFuncs, NULL, init }`. The functions `FooFuncs` names are external:
   keep them to 15 characters (`tools/symcheck.py`).
4. **Hunk.** Compile as programs are (no `-DEXEC_KERNEL`), `ld -r` the
   objects, `foovec.o`, any program library objects and
   `platform/progabs.o`, and convert with `tools/mkhunk.py` (no
   `--entry`). Put it on the disk with `mkdisk.py
   --put=Libs/foo.library=FILE`.
5. **Callers** include `<lvo/foo_lib.h>`, define `struct Library
   *FooBase` and call `FooBase = OpenLibrary("foo.library", 1L)`.

A device is the same with `NT_DEVICE`, `Open(dev, ior, unit, flags)`
(setting `io_Error` on failure), `Close(dev, ior)`, `BeginIO` and
`AbortIO` at −30 and −36 ([`<exec/devices.h>`](../include/exec/devices.h),
`tests/hunks/dltdev.c`), in `Devs/`.

## Root program

After the boot modules the kernel runs the root program, as AmigaOS runs
its startup from disk.

- **Startup file.** The first line of `Startup` on the boot volume
  (at most 14 bytes) names a hunk program in the same root directory. The
  kernel loads it with `InternalLoadSeg` and calls its first hunk,
  `int entry()`, as the root Task on the boot stack (`kstkcfg.sc_root`,
  `ROOTSTKLOW` by default, to `ROOTSTKTOP`; [root stack
  size](#root-stack-size)). The program gets no arguments.
- **Exit.** When the program returns n, the kernel prints `exec900:
  root exit n`, reports stray interrupts, writes the emulator's stop
  doorbell and halts. The seglist is not unloaded. Without a boot volume
  or a `Startup` file, or when the program does not load, the kernel
  prints `exec900: no boot volume`, `exec900: no Startup` or `exec900:
  root NAME not loaded` and stops the same way without a `root exit`
  line.
- `mkdisk.py --root=FILE` puts FILE on the exec partition and names it in
  `Startup`. `exec900.bin` runs `hello`, `make test T=NAME` images run
  the test `NAME`; every image boots the same `exec900.sys`, except
  [tailpad's](internals.md#tests).

Departures: AmigaOS's `S:Startup-Sequence` is a script that dos.library's
shell runs; here `Startup` names one program that exec runs, and the
system stops when it returns. A root program that wants AmigaOS's boot
starts it itself: it opens dos.library, creates a process that opens
`CON:` and calls `Execute("Execute S:Startup-Sequence", in, out)`, and
waits for it (`tests/dosboot.c`).

## DOS

AmigaDOS V36 on the Amiga model: dos.library, packet handlers run as
processes, the DosList and the shell. Upstream's DOS
([dos.md](../../docs/reference/dos.md)) is a reference for structure
only; its streams, pipes, foreground groups and batch calls are not
ported. [Internals](internals.md#dos) has the design.

- **Modules.** `dos.library`, `con-handler`, `v7fs-handler` and
  `ram-handler` are boot modules, listed in that order in `Modules`.
  dos.library's romtag makes the library (`RTF_AUTOINIT`); each handler's
  romtag adds its device to the DosList (`CON`, `DH0` on the boot slot,
  `RAM`) with its seglist, and DOS starts the handler as a process on
  first use. A device listed with `dol_Handler` but no seglist is loaded
  from that file on first use and keeps the seglist, as on AmigaOS.
  Devices given one seglist (`DH0` and `DH1`) run it in a process
  each. The
  shell is
  `L:Shell-Seg` and the commands are in `C:`. `mkdisk.py`'s exec partition
  is the volume `System` with the directories `C`, `S`, `L`, `Libs` and
  `Devs`; `--put=C/Dir=FILE` adds a file. With `--lnames` that volume
  is on a [long-name system volume](#long-name-volumes) instead.
- **Calls** ([`fd/dos_lib.fd`](../fd/dos_lib.fd), `<lvo/dos_lib.h>`, at
  AmigaDOS's LVO offsets). Every call takes `DOSBase` as its hidden first
  argument. Implemented: Open, Close, Read, Write, Input, Output, Seek,
  DeleteFile, Rename, Lock, UnLock, DupLock, Examine, ExNext, Info,
  CreateDir, CurrentDir, IoErr, CreateProc, Exit, LoadSeg, UnLoadSeg,
  DeviceProc, DateStamp, Delay, WaitForChar, ParentDir, IsInteractive,
  Execute, AllocDosObject, FreeDosObject, DoPkt, SendPkt, WaitPkt,
  ReplyPkt, SelectInput, SelectOutput, FGetC, FPutC, UnGetC, FRead,
  FWrite, FGets, FPuts, VFWritef, VFPrintf, Flush, SetVBuf, ExamineFH,
  NameFromLock, SetMode, SetIoErr, Fault,
  PrintFault, Cli, CreateNewProc, RunCommand, Get/SetConsoleTask,
  Get/SetFileSysTask, GetArgStr, SetArgStr, Set/GetCurrentDirName,
  Set/GetProgramName, Set/GetPrompt, SystemTagList, AssignLock,
  AssignLate, GetDeviceProc, FreeDeviceProc, LockDosList, UnLockDosList,
  RemDosEntry, AddDosEntry, FindDosEntry, NextDosEntry, MakeDosEntry,
  FreeDosEntry, IsFileSystem, InternalLoadSeg and InternalUnLoadSeg
  (exec's), CheckSignal, ReadArgs, FindArg, ReadItem, StrToLong,
  MatchFirst, MatchNext, MatchEnd, ParsePattern, MatchPattern, FreeArgs,
  FilePart, PathPart, AddPart, SetVar, GetVar, DeleteVar, FindVar,
  WriteChars, PutStr, VPrintf, SetProtection, SetComment, SetFileDate,
  SetFileSize, SameLock, AssignAdd, RemAssignList, AddSegment,
  FindSegment, RemSegment, SplitName, CompareDates, DateToStr,
  StrToDate, and from V39 ParsePatternNoCase and
  MatchPatternNoCase. Any other vector (SameDevice, ExAllEnd and SetOwner
  among the V39 ones) sets IoErr `ERROR_NOT_IMPLEMENTED` and returns 0.
  The headers are Amiga's: `<dos/rdargs.h>`, `<dos/dosasl.h>`,
  `<dos/var.h>`, `<dos/stdio.h>` and `<dos/datetime.h>`; `<dos/dos.h>`
  adds AROS's `MAXFILENAMELENGTH` (108) and `MAXCOMMENTLENGTH` (80).
- **Native pointers.** A `BPTR` is an ordinary pointer and a `BSTR` a
  NUL-terminated C string (`<dos/dos.h>`), so `BADDR` and `MKBADDR` do
  nothing. The records keep Amiga's shapes and names: `Process`,
  `FileHandle`, `FileLock`, `DosPacket`, `StandardPacket`, `DosList`,
  `DevProc`, `CommandLineInterface`, `DosLibrary`, `RootNode`,
  `DosInfo`, `FileInfoBlock`, `InfoData`, `DateStamp`, `AssignList`,
  `Segment`; `struct PathNode` (`pn_Next`, `pn_Lock`, AllocVec'd) is a
  `cli_CommandDir` entry. `pr_SegList` is
  the process's seglist itself, not a table of them. `ERROR_*` and
  `ACTION_*` are long constants, so they pass as `LONG` arguments.
- **Processes.** `CreateNewProc` takes Amiga's `NP_` tags (`NP_Seglist`,
  `NP_FreeSeglist`, `NP_Entry`, `NP_Input`, `NP_Output`, `NP_CloseInput`,
  `NP_CloseOutput`, `NP_CurrentDir`, `NP_StackSize`, `NP_Name`,
  `NP_Priority`, `NP_ConsoleTask` (default the creator's), `NP_Cli`,
  `NP_Arguments`, `NP_ExitCode`,
  `NP_ExitData`, `NP_CopyVars`; others are ignored) and may be called by a plain Task.
  The record and its `MEMF_STACK` stack are one MemList on
  `tc_MemEntry`. `pr_MsgPort` signals `SIGB_DOS` (8). At exit the process
  closes its streams, unlocks its directory, frees its CLI and argument
  copy, calls `pr_ExitCode(rc, pr_ExitData)` and unloads its seglist, as
  its flags say; then exec removes it. A new CLI copies its parent's
  prompt, current directory name, `cli_DefaultStack` and command path
  (each lock duplicated); its exit frees the path.
- **Programs.** An entry is called as `LONG entry(char *args, LONG len)`:
  the argument string and its length, AmigaDOS's A0 and D0. The shell
  passes the rest of the command line ending in a newline, as AmigaDOS
  does. `RunCommand(seg, stack, args, len)` runs the first hunk in the
  calling process on a new `MEMF_STACK` stack of `stack` bytes (at least
  512) through exec's `StackSwap`, and returns its result, or −1 when no
  stack can be had. `Exit(rc)` returns `rc` from the running command or
  process entry at any depth.
- **Packets.** Every file and lock call is one `DoPkt` with the AmigaDOS
  `ACTION_` code; `DoPkt` waits on `pr_MsgPort` (a plain Task gets a
  temporary port) and sets IoErr from `dp_Res2`. A handler receives
  `(lock, name)` with the name after the device part: the current
  directory and the name for a relative name, the assign's lock for an
  assign, NULL (its root) for a device or volume name or `:name`. A
  handler starts with `ACTION_STARTUP` (`dp_Arg1` the device name,
  `dp_Arg2` `dol_Startup`, `dp_Arg3` the DosList entry) and sets
  `dol_Task` before replying.
- **DosList.** Devices, volumes and assigns, guarded by `di_DevLock`.
  Boot creates `SYS:` → `DH0:`, `C:` → `SYS:C`, `S:`, `L:`, `LIBS:` →
  `SYS:Libs`, `DEVS:` → `SYS:Devs` and `FONTS:` → `SYS:Fonts` as late assigns (`AssignLate`),
  bound to a lock on first use. `AssignLock(name, lock)` makes, replaces
  (and unlocks the old locks) or with NULL removes an assign; it refuses
  a device's or volume's name. `AssignAdd(name, lock)` adds a directory
  to an existing directory assign (the assign owns the lock);
  `RemAssignList(name, lock)` unlocks and removes the first of its
  directories that `SameLock` finds is lock's object, and the assign
  when none is left. `GetDeviceProc(name, dvp)` with the previous
  `DevProc` steps to the assign's next directory. Open, Lock, DeleteFile,
  CreateDir, SetProtection, SetComment and SetFileDate try each directory
  in turn while the handler answers `ERROR_OBJECT_NOT_FOUND` or
  `ERROR_DIR_NOT_FOUND`, so `LoadSeg` (and the shell) search a
  multi-directory `C:`; Rename uses the first. Names are compared
  ignoring ASCII case.
- **Resident list.** `di_ResList` holds `struct Segment` entries
  (`seg_Name` a C string). `AddSegment(name, seg, type)` adds one with
  `seg_UC` = type (0, `CMD_SYSTEM` or `CMD_INTERNAL`);
  `FindSegment(name, after, system)` finds the next system (`seg_UC <
  0`) or user entry; callers `Forbid` while they count a use in
  `seg_UC`, as on the Amiga. `RemSegment` removes and unloads an entry
  whose `seg_UC` is 0, else fails with `ERROR_OBJECT_IN_USE`.
- **Files.** `Read`, `Write` and `Seek` are one packet each, so a C
  library's `read`, `write`, `lseek` and `close` map onto
  `Read`, `Write`, `Seek` and `Close`, `unlink` onto `DeleteFile` and
  `stat` and `fstat` onto `Lock`/`Examine` and `ExamineFH`. `Seek`
  returns the old position or −1. `NIL:` is a handle with no handler;
  `*` is the process's console (`pr_ConsoleTask`). A process without
  one gets the `fh_Type` of its first interactive open, the handler port
  after the open packet, so `*` reaches the process serving that window
  even when a device process hands each open to another one. Closing
  that handle, in any process, clears the console again unless it has
  been changed since; it is not restored, as it was empty. Other opens
  of the same port, `*` among them, leave it alone, so a command
  closing its `*` keeps the shell's console. A child that inherited the
  port keeps it (AmigaOS has the same gap).
- **Buffered I/O.** The buffer belongs to the handle (`fh_Buf`,
  `fh_Pos`, `fh_End`), so a shell and its commands share an input's
  read-ahead. It is allocated by the first buffered call: 512 bytes for a
  file, 256 for an interactive handle, or `SetVBuf`'s size or buffer
  (`SetVBuf` returns 0, or −1 when it fails). Output is `BUF_LINE` on an
  interactive handle and `BUF_FULL` otherwise; `BUF_NONE` writes at once
  (input is still read a buffer at a time). A block of at least a
  buffer's size goes straight to the handler. `Read`, `Write`, `Seek` and
  `Close` settle the buffer first, so buffered and unbuffered calls mix:
  pending output is written; `Read` returns read-ahead first (on an
  interactive handle only that, as a console returns one line); `Write`
  and `Flush` give a file's read-ahead back with a seek, and `Seek`
  returns and moves from the logical position. An interactive handle's
  read-ahead survives `Write`, since console input and output are
  separate streams. `UnGetC(fh, -1)` after the end makes the next
  `FGetC` return the end again. `FPuts`, `VFPrintf`, `VFWritef`,
  `PutStr`, `WriteChars` and `VPrintf` are buffered, as in V36, so a
  program that writes without a newline calls `Flush`; the shell flushes
  `Output()` after each command and before it prompts. `VFWritef` takes
  `%S %Tn %C %N %In %Un %Xn %$`.
- **Arguments.** `ReadArgs(template, array, rdargs)` parses the command
  line (`GetArgStr()`) or `rdargs`'s `RDA_Source` with Amiga's template
  rules: `=` aliases and `/A /K /S /T /N /M /F`, `KEY=value`, one `/M`
  whose last words go to required items after it, and `RDA_Buffer`
  (`RDAF_NOALLOC`). A line of just `?` prints the template and `": "` on
  `Output()` and reads the line to parse from `Input()`; answering `?`
  again prints `RDA_ExtHelp` first, unless `RDAF_NOPROMPT`.
  `RDAF_STDIN` reads the line from `Input()` without a prompt. Errors are
  AmigaDOS's (`ERROR_REQUIRED_ARG_MISSING`, `ERROR_TOO_MANY_ARGS`,
  `ERROR_KEY_NEEDS_ARG`, `ERROR_BAD_NUMBER`, `ERROR_BAD_TEMPLATE`,
  `ERROR_UNMATCHED_QUOTES`, `ERROR_NO_FREE_STORE`), and `Fault` knows
  their texts. `FindArg(template, keyword)` takes the template first, as
  the autodoc and Amiga's implementation do (the NDK fd names D1
  `keyword`). `ReadItem` with a NULL CSource reads `Input()`.
- **Patterns.** `ParsePattern` and `MatchPattern` take `? # #? ( | ) ~
  [ ] [~ ] % '`, and `*` for `#?` when `rn_Flags` has `RNF_WILDSTAR`
  (clear by default); `~x` negates one element. The parsed form is
  private; a buffer of twice the pattern plus 2 bytes holds it.
  `MatchFirst`, `MatchNext` and `MatchEnd` take wildcards in any path
  part, ignore case, fill `ap_Info` and, with `ap_Strlen`, the path in
  `ap_Buf` (`ERROR_BUFFER_OVERFLOW` when it does not fit), set
  `APF_ITSWILD` and `APF_DirChanged`, stop on `ap_BreakBits`
  (`ERROR_BREAK`, `ap_FoundBreak`) and return 0, an IoErr code or
  `ERROR_NO_MORE_ENTRIES`. `APF_DODIR` on a returned directory makes
  `MatchNext` return everything inside it (and in its subdirectories
  given `APF_DODIR` in turn), then the directory again with
  `APF_DIDDIR`, as Amiga's `All` options rely on. `ap_Current->an_Lock`
  is the directory of the match. Call `MatchEnd` after `MatchFirst`
  whatever it returned.
- **Variables.** `SetVar`, `GetVar`, `DeleteVar` and `FindVar` with the
  `LV_VAR` and `LV_ALIAS` types and the `GVF_` flags. Local variables
  (the default for `SetVar`) are `LocalVar` nodes on `pr_LocalVars`; a
  new process copies its creator's unless `NP_CopyVars` is FALSE, and
  frees its own at exit. `GetVar` looks for a local variable, then a
  global one; a text value ends at its first newline; the result is the
  bytes stored and IoErr the value's full length; −1 with
  `ERROR_OBJECT_NOT_FOUND` when there is none. Globals are the files
  `ENV:name` whenever the DosList has an `ENV` entry (a device, volume or
  assign), as on the Amiga; `GVF_SAVE_VAR` also writes `ENVARC:name`
  when `ENVARC` exists. Without `ENV` they live in a list in
  dos.library's memory, which `ENV:` then hides. A global's name is at
  most 56 bytes.
- **CON:** One handler process serves every `CON:` open on
  textcon.device unit 0, so it works on the serial line and on a display
  card; the window specification is ignored. Cooked input is edited as
  it arrives and echoed: Backspace and Delete, Ctrl-X and Ctrl-U clear
  the line, Return (CR, LF or CR LF) ends it, Ctrl-\ ends input (a later
  `Read` returns 0), Ctrl-C to Ctrl-F signal `SIGBREAKF_CTRL_C` to `_F`
  to the process that sent the last packet; cursor up and down (CSI A,
  CSI B) step through the last lines entered (512 bytes of them),
  replacing the line being edited; other escape sequences are dropped.
  A cooked `Read` returns at most one line. `SetMode(fh, 1)`
  makes input raw (unedited, unechoed). `WaitForChar` answers at once;
  `Seek` fails.
- **Boot volume** (`v7fs-handler`): the COHERENT V7 filesystem of
  kboot's boot slot, volume name from the superblock's `s_fname`,
  `id_DiskType` `ID_V7_DISK` ("V7FS"). Locks, Examine/ExNext
  (`fib_DiskKey` is the directory offset), Info, ParentDir, files through
  the double-indirect block (8.4 MB), and writes: `MODE_NEWFILE` creates
  or truncates, `MODE_READWRITE` opens or creates, Write extends,
  DeleteFile, Rename (also of a directory to another parent, or to a new
  spelling of its name) and CreateDir. The free-block chain, the
  free-inode cache and the counts stay as COHERENT's tools expect. An
  exclusive lock or `MODE_NEWFILE` handle excludes every other use, and
  an object in use cannot be deleted (`ERROR_OBJECT_IN_USE`). New files
  get mode 0644, directories 0755; protection bits come from the owner's
  mode bits. The last free block or inode gives `ERROR_DISK_FULL`.
  `SetProtection` sets the owner's mode bits from the clear R (0400),
  W and D (0200, both clear) and E (0100) bits and keeps the group and
  other bits; the H, S, P, A, group and other protection bits are not
  kept, and none is enforced. `SetFileDate` sets the inode's
  modification time (V7 seconds since 1970, so the date's ticks keep
  whole seconds); other changes leave it alone.
- **Long names.** On a [long-name volume](#long-name-volumes) names keep
  up to 30 bytes, AmigaDOS's limit; creating or renaming to a longer
  name fails with `ERROR_INVALID_COMPONENT_NAME`, as on FFS, and a
  lookup of one finds nothing. Info and the volume report `id_DiskType`
  `ID_V7L_DISK` ("V7FL"); AmigaDOS has no name-length field, and its
  long-name FFS variants (`DOS\6`, `DOS\7`) are told apart by type the
  same way.
- **Boot volume cache.** `DH0:`'s and `DH1:`'s `dol_Startup` is a
  `FileSysStartupMsg` (`<dos/filehandler.h>`, native pointers: the
  device name, unit and flags, and a `DosEnvec`). The handler caches
  `de_NumBuffers` 512-byte blocks (default 16, 4 to 120;
  [`DH1:`'s](#long-name-volumes) set by `mkdisk.py --sysbuffers`) of
  `de_BufMemType` memory (default `MEMF_ANY`), 518 bytes each plus up to
  1 KB, and writes changes back later, as FFS does: when a handle is
  closed, on `ACTION_FLUSH`, 1 to 2 seconds after the last packet
  (timer.device), at most 5 seconds after the first unwritten change,
  when the cache needs a changed buffer, and on `ACTION_INHIBIT` and
  `ACTION_DIE`. DeleteFile and truncation by `MODE_NEWFILE` are on the
  disk when they answer; Rename writes the new name before it removes the
  old. Contiguous blocks move in one
  `CMD_READ` or `CMD_WRITE`; a read of file data brings up to 8 blocks
  that follow it on the disk.
- **Consistency across a reset.** Data a closed or flushed handle wrote
  is on the disk. A reset at any other moment loses what was not yet
  written but leaves a filesystem COHERENT's fsck accepts without
  repairing structure: no block is used twice or both used and free, no
  directory entry names a free inode, every reachable directory has its
  `.` and `..`, and every byte inside a file's size is data written
  there. What may remain is what fsck reclaims: blocks and inodes
  allocated for unwritten changes that are neither free nor reachable,
  free counts that do not match, and link counts of an inode whose
  entries were changing. `tools/cohfs.py --check IMAGE` makes the same
  checks. The handler gets this by the order of its writes (data,
  superblock, indirect blocks, inodes, directory entries) and by writing
  removed references before it frees their blocks.
- **Inhibit and die.** `ACTION_INHIBIT` with `DOSTRUE` writes the
  changes, forgets the cache and answers every other packet
  `ERROR_NOT_A_DOS_DISK`; inhibits nest, and the last `DOSFALSE` reads the
  superblock again. `ACTION_DIE` fails with `ERROR_OBJECT_IN_USE` while
  a lock or handle is open (a bound boot assign such as `SYS:` holds one);
  otherwise the handler writes its changes, closes its devices, removes
  its volume, clears `dol_Task` and ends, and the next use of `DH0:`
  starts it again. Packets sent to it after `ACTION_DIE` are lost.
- **RAM:** (`ram-handler`): files and directories in memory on the
  volume `Ram Disk` (`ID_DOS_DISK`), as AmigaDOS's ram-handler. Locks
  (shared and exclusive), Examine/ExNext (`fib_DiskKey` is the entry
  returned last; deleting that entry ends the scan, as on FFS),
  ExamineFH, ParentDir, SameLock, Open in the three modes,
  Read/Write/Seek (not past the end), SetFileSize (an extension reads
  as zeros; handles past the new end move to it), DeleteFile, Rename
  (across directories, not into itself), CreateDir (an exclusive lock,
  as on the Amiga), SetProtection, SetComment (79 bytes), SetFileDate,
  Info (`id_NumBlocks` = `id_NumBlocksUsed`, the 512-byte blocks the
  volume's memory takes) and Flush. Names are at most 30 bytes
  (`ERROR_INVALID_COMPONENT_NAME`), kept as given and compared ignoring
  ASCII case. The R, W and D bits are enforced (`ERROR_READ_PROTECTED`,
  `ERROR_WRITE_PROTECTED`, `ERROR_DELETE_PROTECTED`); `MODE_NEWFILE`
  is exclusive. File bytes are `MEMF_ANY` chunks of 64 to 8,192 bytes,
  each about the file's size so far; deleting or truncating a file frees
  them, and a write that runs out of memory returns the bytes it
  stored, or −1 with `ERROR_NO_FREE_STORE`. The contents go when the
  system stops.
- **Boot assigns.** The boot `S:Startup-Sequence` (the `dosboot`
  image's) does what AmigaOS's does: `MakeDir RAM:T RAM:ENV`, `Assign
  ENVARC: SYS:Prefs/Env-Archive` when that directory exists, `Copy
  ENVARC: RAM:ENV ALL` when `ENVARC:` exists, `Assign ENV: RAM:ENV` and
  `Assign T: RAM:T`.
- **Shell.** `Execute(cmd, in, out)` and `SystemTagList(cmd, tags)`
  (`SYS_Input`, `SYS_Output`, `SYS_Asynch`) run `L:Shell-Seg` as a CLI
  process (loaded once). Execute runs `cmd`, then the commands in `in`;
  SystemTagList runs only `cmd` and returns its result. Each shell's
  state is its own (`pr_ShellPrivate`), so shells started by `Run` and
  `NewShell` run beside it on the one `Shell-Seg`.
  - Built-ins (AmigaOS 3.x's internal commands): `Alias`,
    `Ask` (y: `RETURN_WARN`, n or Return: 0), `CD`, `Echo`,
    `Else`, `EndCLI`/`EndShell`, `EndIf`, `EndSkip`, `Execute`, `Failat`,
    `Fault`, `Get`, `Getenv`, `If`, `Lab`, `NewCLI`/`NewShell` (`WINDOW`, `FROM`, default
    `S:Shell-Startup` when it exists), `Path` (`ADD`, `REMOVE`, `RESET`,
    `SHOW`), `Prompt` (`%N`, `%S`), `Quit`, `Resident` (`REMOVE`, `ADD`,
    `REPLACE`, `PURE`/`FORCE`, `SYSTEM`), `Run`, `Set`, `Setenv`, `Skip`
    (`BACK`), `Stack`, `Unalias`, `Unset`, `Unsetenv` and `Why`. `CD`
    (`DIR`), `Echo` (`/M,NOLINE/S,FIRST/K/N,LEN/K/N,TO/K`: words joined
    by blanks, `LEN` alone the last characters), `Prompt` (`PROMPT`) and
    the variable built-ins parse their arguments with ReadArgs; the
    others take words, a quoted word with AmigaDOS's `*N`, `*E`, `*"`
    and `**` escapes.
  - Variables: `Set`, `Setenv` and `Alias` take `NAME,STRING/F` and set
    a local variable, a global one or an alias; given only `NAME` they
    show it, given nothing they list the local variables, the files in
    `ENV:` or the aliases. `Get` and `Getenv` (`NAME/A`) print a local or
    global value, or give `RETURN_WARN` when there is none; `Unset`,
    `Unsetenv` and `Unalias` (`NAME`) delete. Before a line runs, `$name`
    and `${name}` become the variable's value (`GetVar`: local, then
    global; a name not set stays as written) and `*$` a `$`; then an
    alias in the command's place becomes its value, `[]` in it (or its
    end) taking the rest of the line, once.
  - A command name is looked up in the resident list (user entries),
    then the built-ins, then loaded from the current directory, each
    `Path` directory (`cli_CommandDir`) and `C:`; a name with a device
    or path part is loaded as given. A resident command's `seg_UC` counts
    the run. Commands run with `RunCommand` on `cli_DefaultStack`
    longwords (4,000 bytes by default); `Stack n` sets it (at least
    1,600 bytes), also from a script such as `S:Startup-Sequence`, and
    `NewShell` and `Run` copy it. `>name` and
    `<name` redirect a command's or built-in's output and input, except
    `Run`'s, whose line goes to the new shell whole. The result becomes
    `cli_ReturnCode` and IoErr `cli_Result2`, which `Why` explains.
  - Scripts nest: `Execute` reads its script until the end, then input
    returns to the script or standard input that ran it. In a script, a
    result at or above `cli_FailLevel` (10, `Failat`) prints "*name*
    failed returncode *n*" and ends every script, as does Ctrl-D
    (`***Break`); `Quit [n]` ends every script with result n.
  - Dot lines: `.key` (`.k`) takes a template of names with `/A`, `/K`
    and `/S` (a switch's value is its name); `<name>` and
    `<name$default>` on later lines become the arguments, `<$$>` the CLI
    number; `.bra`, `.ket` and `.dollar` (`.dol`) change `<`, `>` and
    `$`; `.default` (`.def`) gives a name's default; `. text` is a
    comment. A bracketed name that is not a key stays as written.
    Missing `/A` arguments or extra ones end the scripts with
    `RETURN_FAIL`.
  - `If [NOT] [WARN|ERROR|FAIL] [a EQ|GT|GE b [VAL]] [EXISTS name]`
    skips to its `Else` or `EndIf` (nesting counted) when false; `Else`
    skips to `EndIf`. `Skip [label] [BACK]` skips to `Lab label` (from
    the script's start with `BACK`), or without a label to `EndSkip`; a
    label not found ends the script with `RETURN_ERROR`. `If`, `Else`,
    `EndIf`, `Skip`, `Lab` and `EndSkip` keep `cli_ReturnCode`, so a
    second `If` tests the same result. EQ, GT and GE compare strings
    ignoring ASCII case, or numbers with `VAL`.
- **Commands** in `C:` parse their arguments with ReadArgs and AmigaOS
  3.x's templates (so `?`, `KEY=value` and quoting work):
  - `Copy FROM/M,TO/A,ALL/S,QUIET/S,BUF=BUFFER/K/N,CLONE/S,DATES/S,
    NOPRO/S,COM/S,NOREQ/S`: a file to a file, files into a directory, a directory's
    files (with `ALL` its subdirectories) into a directory made if
    missing, listing them unless `QUIET`. Protection is copied unless
    `NOPRO`, dates with `DATES`, comments with `COM`, both with `CLONE`;
    `BUF` and `NOREQ` are ignored.
  - `Delete FILE/M/A,ALL/S,QUIET/S,FORCE/S`: with `ALL` a directory's
    contents first, listing them unless `QUIET`; `FORCE` clears the
    protection first. A name not deleted gives `RETURN_WARN`.
  - `Rename FROM/A/M,TO=AS/A,QUIET/S`: into `TO` when it is a directory.
  - `MakeDir NAME/M/A`.
  - `Assign NAME,TARGET/M,LIST/S,EXISTS/S,DISMOUNT/S,DEFER/S,PATH/S,
    ADD/S,REMOVE/S,VOLS/S,DIRS/S,DEVICES/S`: without a name lists volumes, assigns (each directory of a
    multi-directory one on a `+` line, a late one as `<path>`) and
    devices (`LIST`, `VOLS`, `DIRS`, `DEVICES` choose); `Assign X:`
    removes X; `Assign X: dir...` makes a multi-directory assign,
    `ADD` adds to it, `REMOVE` takes a directory out, `DEFER` makes a
    late assign (`PATH` too), `EXISTS` gives `RETURN_WARN` when X is not
    listed; `DISMOUNT` is ignored.
  - `Protect FILE/A,FLAGS,ADD/S,SUB/S,ALL/S,QUIET/S`: sets the bits
    named (`[+|-]hsparwed`: h, s, p, a set; r, w, e, d allowed), or adds
    or takes them away; with no flags, shows them. `ALL` is ignored.
  - `Info DEVICE`: size, blocks used and free, use, errors, state and
    volume of each started filesystem, then the volumes.
  - `Version NAME,VERSION/N,REVISION/N,FILE/S,FULL/S,RES/S`: Kickstart's
    (exec.library's) version; a library's or device's; or a file's
    `$VER:` string; `RETURN_WARN` when older than n.
  - `Date DAY,DATE,TIME,TO=VER/K` (`DD-MMM-YY`, `HH:MM[:SS]` in any of
    the three): shows "Weekday DD-MMM-YY HH:MM:SS" (into `TO`) or sets
    the system time and the battery clock (AmigaDOS has `SetClock` for
    the latter); a weekday name is not understood.
  - `Wait /N,SEC=SECS/S,MIN=MINS/S,UNTIL/K` (`HH:MM`): Ctrl-C ends it
    with `RETURN_WARN`.
  - `Dir DIR,OPT/K,ALL/S,DIRS/S,FILES/S,INTER/S` (`OPT A`, `D`, `F`; a
    pattern in `DIR`'s last part; `INTER` is ignored).
  - `Type FROM/A/M,TO/K,OPT/K,HEX/S,NUMBER/S` (`OPT H`, `OPT N`).
  - `StackUse` (no arguments; an Exec900 command, AmigaOS has none of
    the name): one line per Task, the running one, then `TaskReady` and
    `TaskWait` in list order: name, `tc_SPLower`'s offset (hex),
    `tc_SPUpper - tc_SPLower`, deepest use, free bytes and state (`run`,
    `ready`, `wait`). Use is the stack's top less the first nonzero byte
    above `tc_SPLower + 8` (the guard word, or an allocator's chunk
    header on a stack allocated without `MEMF_CLEAR`, is skipped); a
    stack not cleared when made reads as used. A process running a
    command on its own stack (`RunCommand`) gets a second line, state
    `process`, for its process stack (`pr_StackBase`, sized from its
    `tc_MemEntry` entry). The last line is the `MEMF_STACK` pool's free,
    total and largest bytes. It collects under `Forbid` and `Disable`
    and scans the stacks under `Forbid`; the idle Task and the interrupt
    stack are not listed.
- **Time.** `DateStamp` reads the system time (timer.device's
  `GetSysTime`, seconds since 1978-01-01, Amiga's epoch); `dl_TimeReq`
  is dos.library's binding. `Delay` takes Amiga ticks (1/50 s) and
  sleeps twice as many 100 Hz ticks.
- **Dates.** `DateToStr` writes the weekday, the date in
  `FORMAT_DOS` (`15-Nov-26`), `FORMAT_INT` (`26-11-15`), `FORMAT_USA`
  (`11-15-26`) or `FORMAT_CDN` (`15-11-26`), and the time (`12:34:56`),
  skipping a NULL string; it fails for a negative day. With `DTF_SUBST`
  the date is `Today`, `Yesterday` or `Tomorrow`, or the weekday for a
  day 2 to 6 days back (forward with `DTF_FUTURE`). `StrToDate` reads
  those formats (1 or 2 digit fields, `-` or `/`, a 2 or 4 digit year,
  any case) and the names, a weekday meaning the past week (the coming
  one with `DTF_FUTURE`), and the time as `hh:mm` or `hh:mm:ss`; a NULL
  string keeps that part of `dat_Stamp`, and a bad one fails, leaving
  it. Two-digit years 78 to 99 are 19xx, 00 to 77 20xx. There is no
  locale.library: `FORMAT_DEF` is `FORMAT_DOS` and the names are English.
  `CompareDates` is negative when the first date is later. `SplitName`
  copies the part of `name` from `oldpos` to the separator, at most
  `size` − 1 bytes, and returns the position after the separator or −1
  at the end of the name.

Departures from AmigaDOS, and why:

- Native pointers instead of BPTRs and BSTRs (the user's decision): the
  Z8001 has no use for longword-shifted pointers, and C callers need
  none.
- DOS is disk-loaded boot modules, not ROM: kernel text is one 64 KB
  segment. The boot assigns are late assigns because the boot volume's
  handler is a later module and no process exists while modules
  initialise.
- One CON: handler for every open (Amiga starts one per window): there
  is one console and no windows. `WaitForChar` does not wait.
- The V7 handler orders its delayed writes so that a reset needs no
  repair before the volume is used again; FFS instead marks the volume
  invalid and has the validator rebuild its bitmap, and there is no V7
  validator here. An inhibited handler answers `ERROR_NOT_A_DOS_DISK`.
- The filesystem is COHERENT V7, not FFS: on a standard V7 volume names
  keep their first 14 bytes (V7's `namei` does the same), so `S:Startup-Sequence` is the file
  `S/Startup-Sequen`; there are no comments, links or `SetFileSize`, and
  protection is three mode bits, unenforced, as above. Opening a
  directory as a file fails (`ERROR_OBJECT_WRONG_TYPE`).
- RAM: names longer than 30 bytes fail with
  `ERROR_INVALID_COMPONENT_NAME` (FFS cuts them): a silent cut could
  make two names one. RAM: reports `ID_DOS_DISK`.
- `NP_Synchronous`, `NP_NotifyOnDeath`, `NP_Path` and `NP_HomeDir`
  are ignored; `Execute` waits for its shell, and the
  handles it is given stay the caller's.
- Default stacks are smaller than Amiga's 4,000 bytes (table above): the
  stack region is about 58 KB for every Task with the default 63 KB
  segment.
- Shell: `Setenv` with no arguments lists only `ENV:`'s files, so it
  shows nothing for globals kept in dos.library's list (no `ENV:`).
  `Execute` is a built-in (AmigaOS
  3.x has `C:Execute`, which writes the script to `T:`): the shell reads
  its input ahead, so a separate command could not hand it the rest of
  a script. The built-ins are not entries of the resident list
  (`Resident` lists no `INTERNAL` lines). `Run` prints no CLI number.
  `NewShell` opens its window once, as AmigaOS does: the input's
  `fh_Type` becomes the new shell's console and its output is `*` on
  it (a non-interactive `WINDOW` is opened twice); the window does not
  become the console of the process running `NewShell`. The
  built-ins other than `CD`, `Echo`, `Prompt` and the variable ones
  parse their arguments themselves (no `?` help or `=` forms).
- `If`, `Else`, `EndIf`, `Skip`, `Lab` and `EndSkip` keep
  `cli_ReturnCode`, so consecutive `If`s test one command's result.
- `Protect` with no flags shows the bits, since there is no `List`.
- A resident command must be pure (the P bit, or `PURE`) as on the
  Amiga; the C: commands here keep state in their data and bss, so
  they are not.
- Commands get their arguments as a string (`GetArgStr`), not in their
  input's buffer, so `ReadArgs` parses that string; the `?` prompt is
  given only for a line that is just `?`. Without an `ENV` entry in the
  DosList (before `S:Startup-Sequence` assigns `ENV:`), global
  variables live in dos.library's memory instead of failing.

### Long-name volumes

An Exec900 variant of the COHERENT V7 filesystem, so that names such as
`ClickToFront.info` fit: Workbench needs `NAME.info` beside `NAME`, and
14 bytes cut most of them. Everything is V7's layout except:

| Where | Standard V7 | Long-name variant |
| --- | --- | --- |
| Directory entry | 16 bytes: inode (2), name (14) | 32 bytes: inode (2), name (30) |
| Superblock `s_fsize` (offset 2) | the volume's blocks | 0 |
| Superblock bytes 500-503 | unused | `E9LN` |
| 504-507 | unused | the volume's blocks (PDP order, as `s_fsize`) |
| 508-509 | unused | the entry size, 32 (little endian) |

A name is NUL-padded and has no terminator when it is 30 bytes. Bytes
500-511 lie past COHERENT's `struct filsys` (500 bytes).

`s_fsize` 0 makes COHERENT refuse the volume instead of misreading it.
COHERENT has no filesystem magic; its `mount`, `df` and compress's
filesystem test reject a superblock whose `s_isize` is not below
`s_fsize`, and `icheck` and `dcheck` (so `check`) stop with "Ridiculous
fsize/isize". Read with 16-byte entries, the second half of a long entry
would name a random inode, and `check -s` would then corrupt link
counts. COHERENT's kernel mount itself does not check, but its balloc
and bfree refuse every block of a zero-size volume.

`v7fs-handler` picks the kind from the superblock at mount (and after
an inhibit): both kinds work in one system. `tools/cohfs.py` writes,
reads (`read_tree`) and checks (`--check`) both.

**Layout.** kboot finds `exec900.sys` with the boot ROM's 16-byte
directory lookup, and the kernel's module loader reads 16-byte entries
too, so a long-name boot volume would need both changed. Instead
`mkdisk.py --lnames` keeps slot 14 a standard V7 volume `Boot` (the
kernel, `Modules`, `Startup`, the modules and the root program) and puts
the system volume `System` (`C`, `S`, `L`, `Libs`, `Devs` and every
`--put`) in slot 13, long-name, right after it:

```
block 0     136   boot   kboot + kboot.cfg (part 13 and 14 lines)
      136   1024  slot 14  DH0: Boot    standard V7
      1160  1024  slot 13  DH1: System  long names = SYS:
      2184  64+64 slots 0, 1  test pattern, scratch
```

**Mount.** When `wd.device` opens slot 13 (kboot.cfg has a `part 13`
line), v7fs-handler's romtag lists `DH1` with a `FileSysStartupMsg` for
`WD_SLOT(0, 13)` (`de_DosType` `ID_V7L_DISK`, informational: the handler
reads the kind from the disk) and the same seglist as `DH0`, and
replaces the boot `SYS:` assign with `SYS:` → `DH1:`. As with AmigaOS's
FileSystem.resource, the mounter hands DOS a loaded handler's segment
for a second device; DOS itself does not share a seglist between
devices that name the same `dol_Handler` file, since a handler's code
need not serve two volumes. DOS starts a process on the seglist for
each device on its first use, each with its own `ACTION_STARTUP`
packet, 1,280-byte stack and per-volume state: `DH1:` alone does not
start `DH0:`'s handler. On a disk without slot 13 nothing changes.

**Buffers.** `DH1:`'s `de_NumBuffers` is 16 by default, as `DH0:`'s:
`DH1:` holds `SYS:`, the volume a running system uses, and `DH0:`'s
handler does not run unless something names `DH0:`.
`mkdisk.py --sysbuffers=N` (4 to 120) sets it in the disk's copy of
`v7fs-handler`.

**Cost** (`tests/v7share`, 1 MB). Each running volume takes 11,240 heap
bytes with 16 buffers (9,312 of them the cache; 8 buffers save 4,144)
and its process stack; the handler's code (16,180 + 420 + 4 bytes) is
loaded once. Before the shared seglist, `DH1:`'s first use cost 37,760
bytes (a second handler, both caches) and two handler stacks.

## C library

Ordinary C programs run as DOS commands on the Mark Williams COHERENT C
library that `commodore-900-toolchain` ships (`src/libc`), compiled for
Exec900 with its system-call layer and startup replaced by `libc/` over
dos.library and exec ([internals](internals.md#c-library)). The COHERENT
sources are BSD 3-Clause: [`libc/NOTICE`](../libc/NOTICE) must accompany
any distributed program.

- **Building.** `make -C c900 libc` builds `$(OUT)/lib/libc.a`, the
  startup `$(OUT)/obj/lx_crt0.o` and `$(OUT)/lib/nofloat.o`. `make -C
  c900 cprog SRC=/abs/prog.c [NOFLOAT=1]` builds the hunk file
  `$(OUT)/cprog/prog` (`SRC` is absolute or relative to `c900/`). By
  hand: compile with `cc2-z8001 0012` and the include path
  `-Ic900/libc/include -Ic900/include -I$(OUT)/include
  -I$TOOLCHAIN/src/include -I$TOOLCHAIN/src`, link `ld -r lx_crt0.o
  prog.o [nofloat.o] libc.a progabs.o` and convert with `mkhunk.py
  --entry=start`. `mkdisk.py --put=C/Prog=FILE` installs it.
- **Headers** are the toolchain's, plus `libc/include/errno.h`, which
  adds `ENOSYS` (89). A program may also include the exec and DOS
  headers and call dos.library through `DOSBase`, which startup opens.
- **Startup.** `main(argc, argv, envp)`. `argv[0]` is the command name
  (`GetProgramName`, empty outside a CLI). The argument line splits at
  blanks; a word starting with `"` runs to the next `"`, inside which
  `*"` is `"`, `**` is `*`, `*N` a newline and `*E` an escape, as
  AmigaDOS's C startup. `environ` starts empty (`environ[0]` is NULL);
  `getenv` returns a name `putenv` set in `environ`, else the DOS
  variable's value (`GetVar`: a local variable, then a global one) in a
  buffer the next `getenv` replaces. There is no `setenv` or `unsetenv`
  (MWC declares none).
- **Exit.** Returning from `main` calls `exit`. `exit` runs the `atexit`
  functions (at most 32) in reverse, flushes and closes stdio, then
  `_exit`: closes the descriptors the program opened, returns to the
  directory it started in, frees all `malloc` memory, closes
  dos.library and returns the status as the command's result, unchanged,
  so a script's fail level applies (use 0, 5, 10, 20). `abort` is
  `_exit(20)`.
- **Descriptors.** 20. Descriptors 0, 1 and 2 are `Input()`,
  `Output()` and `pr_CES` (else `Output()`); they are the caller's and
  never closed. `open` maps `O_CREAT|O_TRUNC` to `MODE_NEWFILE`,
  `O_CREAT` alone to `MODE_READWRITE`, otherwise `MODE_OLDFILE`;
  `O_TRUNC` on an existing file takes `MODE_NEWFILE`, `O_EXCL` fails
  with `EEXIST` on an existing name; the mode argument is ignored.
  `MODE_NEWFILE` is exclusive on the boot volume, so a file being
  written (`creat`, `fopen` `"w"`) cannot be opened again until closed
  (`EBUSY`). `O_APPEND` seeks to the end before each write. Each `read`,
  `write` and `lseek` is one DOS call; a console `read` returns at most
  a line. `lseek` past the end fails (`EINVAL`), on a console with
  `ESPIPE`. `isatty` is `IsInteractive`.
- **stdio** is COHERENT's, with its own buffers over `read` and
  `write` rather than dos.library's buffered calls (faster:
  [internals](internals.md#c-library)): stdout is line-buffered on a
  console and buffered in 512 bytes otherwise; stdin is unbuffered on a
  console; stderr is unbuffered. A program mixing stdio and buffered DOS
  calls on one stream flushes each before using the other. `printf` is MWC's: `%d %u %o %x %c %s %e %f %g`,
  `l`, width, precision and the `-` and `0` flags; `%D %U %O %X` are
  long conversions, so `%X` is long hexadecimal; there are no `+`,
  space or `#` flags. `sprintf` returns its buffer. Floating-point
  conversions cost about 3 KB; `nofloat.o`, linked ahead of `libc.a`,
  replaces them with a stub that reports `No floating point!` and exits.
- **Doubles.** The compiler calls the runtime for `double` arithmetic.
  Add, subtract, multiply and divide run on the EPU when `AttnFlags` has
  `AFF_68881`, rounding to nearest, with F0 as scratch; otherwise they
  are the toolchain's software routines, which truncate. Conversions and
  comparisons are always software.
- **Names** are AmigaDOS paths, passed unchanged: `SYS:C/x`, `x`
  relative to the current directory, a leading `/` for the parent, `:`
  for the volume's root. There is no Unix root, `.` or `..`.
- **Files.** `unlink`, `remove` and `rmdir` are `DeleteFile`; `mkdir`
  is `CreateDir`; `rename` is `Rename`, deleting an existing new name
  first (not atomic). `stat` and `access` use `Lock` and `Examine`,
  `fstat` `ExamineFH`: `st_mode` from the entry type and the owner's
  protection bits, `st_size`, `st_ino` (`fib_DiskKey`) and the three
  times from the entry's date; a handle that cannot be examined (a
  console, `NIL:`) is `S_IFCHR`. `chdir` is `CurrentDir` (restored at
  exit), `getcwd` and `getwd` are `NameFromLock`.
- **errno** comes from `IoErr()`: not found and unmounted give `ENOENT`,
  exists and directory not empty `EEXIST`, in use `EBUSY`, no store
  `ENOMEM`, disk full `ENOSPC`, wrong type `ENOTDIR`, write-protected
  `EROFS`, read- or delete-protected `EACCES`, across devices `EXDEV`,
  too large `EFBIG`, seek and name errors `EINVAL`, unknown actions
  `ENOSYS`, the rest `EIO`.
- **Time.** `time` and `ftime` read `DateStamp` (the system time, to
  1/50 s),
  taken as UTC unless the variable `TIMEZONE` (COHERENT's form) is set;
  without it `timezone` is 0. `sleep` is `Delay`.
- **Memory.** `malloc`, `free`, `realloc` and `calloc` use an exec pool
  made at the first `malloc` (2 KB puddles; blocks over 1 KB get their
  own `AllocMem`) and deleted at exit. A block lies in one segment, so
  the largest is 65,500 bytes; pointer arithmetic stays within a block.
  `sbrk` and `brk` fail with `ENOMEM`.
- **Process.** `system(cmd)` runs `cmd` through `SystemTagList` on the
  caller's streams and returns its result, not a wait status.
  `getpid` is 1, the user and group ids are 0, `umask` and `sync` do
  nothing.
- **Unsupported:** fork, the exec family, `wait`, `pipe`, `popen`,
  `signal` (returns `SIG_ERR`), `kill`, `alarm`, `pause`, `dup`, `dup2`,
  `fcntl`, `ioctl`, `link`, `chmod`, `chown`, `utime`, `uname`, `times`,
  `mknod`, `mount` and the other COHERENT system calls fail with
  `ENOSYS`, which `strerror` and `perror` report as "function not
  implemented". `strerror` returns `sys_errlist`'s lower-case texts, as
  `perror` prints them. `tmpnam` and
  `tempnam` name `/tmp`, which does not exist.
- **Limits.** Each hunk is at most 65,520 bytes; a program whose code,
  initialised data or bss is larger is linked with `mkhunk.py --split`
  ([over 64 KB](#program-loading)). Commands run on the shell's
  `cli_DefaultStack` (4,000 bytes): `tests/libc`'s `CTest` uses 816 of
  them, so large locals belong in static storage or `malloc`.
- **Sizes** (hunk code + data + bss bytes, plus 8 bytes of loader header
  per hunk): a `printf("hello, %s %d\n", ...)` program is 9,946 + 994 +
  206, or 6,962 + 774 + 168 with `nofloat.o`. At run time it adds the
  first 2,080-byte puddle (the stdout buffer and argv) and the pool
  header.

## Network

`lance.device` is the [LANCE card](hardware.md#lance-card)'s SANA-II
driver, modelled on the A2065's `a2065.device`. It is a boot module:
`mkdisk.py --module=$(OUT)/hunks/lance.device` lists it in `Modules`.
Without the card it prints `lance.device: no card` and adds no device,
so OpenDevice fails with `IOERR_OPENFAIL`. `<devices/sana2.h>` holds the
Amiga definitions.

- **Commands.** `CMD_READ` (by `ios2_PacketType`; a type of 1500 or
  less takes any IEEE 802.3 frame), `S2_READORPHAN`, `CMD_WRITE`,
  `S2_BROADCAST`, `S2_MULTICAST`, `S2_DEVICEQUERY` (MTU 1500, RawMTU
  1514, 10 Mbit/s, `S2WireType_Ethernet`), `S2_GETSTATIONADDRESS`
  (current in `ios2_SrcAddr`, the PROM's in `ios2_DstAddr`),
  `S2_CONFIGINTERFACE` (once; it goes online), `S2_ONLINE`,
  `S2_OFFLINE`, `S2_ONEVENT`, `S2_ADDMULTICASTADDRESS`,
  `S2_DELMULTICASTADDRESS` (counted per address, eight addresses),
  `S2_TRACKTYPE`, `S2_UNTRACKTYPE`, `S2_GETTYPESTATS` (eight types),
  `S2_GETGLOBALSTATS`, `S2_GETSPECIALSTATS` (the three Ethernet
  records), `CMD_FLUSH` and AbortIO, with the Amiga errors and wire
  errors. `SANA2IOF_RAW` reads and writes whole frames; received frames
  set `SANA2IOF_BCAST` or `SANA2IOF_MCAST`. Every opener with a matching
  read gets a copy of a frame; a frame no read takes goes to the first
  orphan read and counts in `UnknownTypesReceived`.
- **Openers.** `ios2_BufferManagement` holds the tag list at OpenDevice
  and the driver's opener record afterwards; copy it into every request.
  `S2_CopyToBuff16` and `S2_CopyFromBuff16` are used in preference to
  the byte versions: the driver's side of a copy is always word aligned.
  `SANA2OPF_MINE` opens exclusively; `SANA2OPF_PROM` makes the chip
  promiscuous while that opener is open, and every opener then sees every
  frame its reads match. The unit is configured and online from
  `S2_CONFIGINTERFACE` until the last close, which takes it offline,
  frees its buffers and forgets the configuration, the multicast
  addresses, the tracked types and the statistics.
- **Offline.** Reads and writes are refused with `S2ERR_OUTOFSERVICE` /
  `S2WERR_UNIT_OFFLINE`; going offline returns every pending read and
  write that way. `S2_ONEVENT` completes at once for the state the unit
  is in.
- **Memory.** The first open allocates 15,312 bytes of `MEMF_24BITDMA`
  memory: the init block, 8 receive and 2 transmit descriptors and
  buffers of 1,520 bytes. The device base is 510 bytes and each opener
  34. No Task and no stack-segment memory: the server runs on the
  interrupt stack.

Departures, and why:

- The buffer functions are C functions, `int CopyToBuff(to, from,
  ULONG n)` and `int CopyFromBuff(to, from, ULONG n)`, nonzero on
  success, and the packet filter is a `struct Hook` called as
  `h_Entry(hook, ios2, data)` (`data` is the payload, or the frame for a
  raw read): MWC has no register calling convention. The driver calls
  them from its interrupt server and from BeginIO with interrupts off,
  so they must be short and must not wait, as on the Amiga.
- `S2_CopyToBuff32`, `S2_CopyFromBuff32` and the DMA variants are
  ignored: the Z8001 has no 32-bit bus, and the chip cannot reach an
  opener's buffers through the MMU.
- Units 1 and 2 are C900 additions: the same card in the chip's internal
  (LOOP|INTL) and external (LOOP) loopback modes, for diagnostics. Only
  one unit is open at a time (`IOERR_UNITBUSY`). Loopback frames are at
  most 32 bytes including the header and are not padded; unit 0 pads
  frames to 60 bytes.
- A frame larger than a receive buffer is an error (counted in
  `BadData`), as the chip is not programmed to chain buffers.

## Unsupported and deferred

These are not provided. Calls and flags for them must not be assumed to
work.

- Blocks over 64 KB outside the identity heap: from relocation slots,
  the stack region or the physical-only pool.
- DOS beyond [the calls listed](#dos): writable filesystems other than
  the boot volume and RAM:, pipes, path assigns (`AssignPath`),
  notification, record locks, `SameDevice`, `ExAllEnd`, `SetOwner`,
  `VFWritef`'s `%O`, `ExAll`, `OpenFromLock`, `Relabel`, links, the
  Workbench and requesters. A V7 name longer than 14 bytes is cut to 14,
  and one longer than 30 is refused on a long-name volume ([DOS](#dos)).
- Booting from a long-name volume: kboot and the kernel's module loader
  read only standard V7 ([long-name volumes](#long-name-volumes)).
  COHERENT cannot read or mount a long-name volume.
- The block cache; `wd.device` formatting, write-protect query, drive
  parameter setting and drive 1/3 access on the emulator (it ignores the
  LUN, so drive 1 is drive 0's image). Disk and floppy timing, real error
  statuses and media change have been exercised only as the emulator
  produces them (immediate completion, 0x92 for no floppy).
- `timer.device` completion between ticks, and sub-tick clock reads on
  the C emulator. The sub-tick path has run only on the Go model.
- Battery-clock times before 1978 or after 2077; the MSM58321's standard-signal output and BUSY line (not wired). The clock has
  been exercised only on the emulator.
- `serial.device` XON/XOFF, 7-wire handshake, mark/space parity,
  `CMD_STOP`/`CMD_START` and rate or framing changes on line 0. Transmit,
  Ext/Status and special-condition interrupts, modem lines and breaks have
  not been exercised: the emulator raises only receive interrupts.
- `parallel.device` reads, `CMD_STOP`/`CMD_START`, SELECT and PAPER OUT
  status (not wired) and waiting for a printer longer than 10 minutes;
  IEEE-488 on CIO U66. `beep.device` plays one square wave at a time, with
  no volume, waveform or audio.device interface.
- NVI delivery has not been exercised on hardware; the emulator never
  raises it.
- EPU exception interrupts (USER trap enables, SC MIE), overlap modes,
  EPU use in interrupt code, a second EPU, and an emulator for extended
  instructions without an EPU. Loading a saved signalling NaN may quiet
  it. The EPU path has not run yet: the emulator and the Go model have
  no EPU, and `tests/epu` on the core bench (`tb_core +fpu`, 6 MHz) is
  still to be run.
- The C library calls listed as unsupported under
  [C library](#c-library), and C library programs that are not DOS
  commands or processes.
- console.device without Workbench; textcon.device windows, units
  other than 0, keymaps, raw input events, insert/delete characters, insert lines and scroll down; the LR console
  has run only on the Go model. A display without its module on the boot
  volume: the console stays on serial.
- HR display: blink, characters outside ASCII 0x20-0x7E (drawn as
  `?`), colour selection; only 1024x800 has run (Go model), and no mode
  on a monitor.
- gameport.device: units other than 0, joysticks, input.device, a
  pointer; `GetGPButtons` has run on HR only on the Go model; an HR card that is not the console; detecting whether a
  serial mouse is connected; other serial mouse protocols (Microsoft,
  PS/2) and other lines. The HR mouse has run only on the Go model, the
  serial mouse only on the emulator.
- framebuf.resource: a mode list for LC (the owner chooses), a second
  head (an HR card beside an LR or LC console), an LC drawing path, a
  pointer, alert
  display while lent, and tracking an owner that exits. It has run only
  on the emulator (absent, or its LC register model in both LC modes)
  and the Go model (HR 1024x800); colour, 1024x1024 and LC on a board
  are untested.
- LC display: a console mode chosen after boot; 640x400 needs the
  interlace the core weaves from C900_1611d1e on and has run only on the
  emulator's register model and the 8568 RTL bench.
- Keyboard: reset handlers (`KBD_ADDRESETHANDLER` and its pair), keymap
  changes, cooked function keys and Alt; the keyboard has been exercised
  only on the Go model.
- MMU protection beyond the inhibited segments, user (normal) mode and
  per-Task address spaces.
- Trapping accesses to segments 0x40–0x7F: the MMU does not translate them.
- Heap above physical 0x3F beyond the pages the relocation slots hold:
  the rest of that RAM is only a page pool reached through windows.
- Stack overflow prevention inside the stack pool: the probe reports an
  overrun after the fact.
- More than 63 KB of stack segment, or a stack segment per Task
  ([design note](internals.md#beyond-one-stack-segment)).
- `lance.device`: `S2_ADDMULTICASTADDRESSES`, `S2_DELMULTICASTADDRESSES`
  and the other revision 4 commands, NSD queries, more than eight
  multicast addresses or tracked types, frame chaining; it has run only on
  the Go model, whose cable has no internal loopback.
- Hardware and MiSTer qualification; only the emulator has been exercised.
