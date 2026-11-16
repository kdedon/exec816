# Exec900 internals

Exec900 is the Commodore 900 (Zilog Z8001) build of Exec816. The kernel
policy is ported from the Action! sources to K&R C for the Mark Williams C
compiler in `commodore-900-toolchain`. This page records the machine model and
the conventions every C900 source follows.

## Toolchain facts

- Compile: `cc0-z8001 800000020800 X.c X.z0 -I... ; cc1-z8001 800000020800
  X.z0 X.z1 ; cc2-z8001 0012 X.z1 X.o X.scr 0`. Assembly: `cpp
  -traditional-cpp -P -I... X.s > X.i ; as-z8001 -g -o X.o X.i`. Link:
  `ld-z8001 -i -L -e start -R 0x30000000 -o exec900.sys crt.o ...`
  (`crt.o` first). Tools live in `$C900_TOOLCHAIN/host/build`
  (`z8001/cc*-z8001`, `as-z8001`, `ld-z8001`, `tools/loutdis`).
- K&R C only: no prototypes and no `//` comments. `void`, `const`,
  `volatile`, struct assignment, enums and bitfields work.
- `int` is 16 bits, `long` 32 bits, and pointers 32-bit segmented
  (`seg << 24 | offset`). Pointer arithmetic changes only the offset word, so
  objects never cross a 64 KB segment, except `AllocMem` blocks over
  64 KB, which are stepped with `PtrStep`. Structs are word-aligned.
- Because there are no prototypes, every public call is a macro that
  casts its arguments to their declared widths. `tools/genlvo.py`
  generates them from `fd/exec_lib.fd` into `<lvo/exec_lib.h>`: in
  programs they call through exec.library's jump table, in kernel sources
  (built with `-DEXEC_KERNEL`) they call the `x`-prefixed implementation
  ([libraries](platform.md#libraries-and-sysbase)). Kernel code calling
  another kernel function directly must pass correctly sized arguments:
  `0L` or `NULL` for pointers and `(ULONG)` for masks and sizes.
- Toolchain v0.2.0 or later is required ([DEPS](../../DEPS) takes the latest). It fixes cc1's internal
  error 175C on an indirect call through an address computed from a local
  or register variable, a compare against `&h->a[i]->first` that
  branched on the flags of a following `dec` (`tests/ccfix.c` checks
  both), internal error EDFA69 on a cast to `LONG` of a pointer
  expression with a side effect or arithmetic (`(LONG)p++`), and cc0's
  macro expansion buffer overflowing on an argument of a few hundred
  characters (about 1,400 now fit). `(long)p == (long)b + 1` still stops
  with EDFA55: compare through a variable.
- libc's soft-float add, subtract, multiply and divide round to nearest,
  ties to even, and `modf` truncates toward zero, as ANSI says.
- External symbols are truncated to 15 significant characters, plus the
  trailing `_`. Keep external names to 15 characters or
  fewer. The limit is the 16-byte name field of the l.out symbol record,
  which the assembler, linker, archive index and COHERENT tools share, so
  it stays. cc2 warns when it truncates a name, or when two names in one
  object truncate to the same symbol; collisions across objects need
  `tools/symcheck.py`. Code that needs longer externals renames them with
  a generated `#define` header ([long external names](#long-external-names)).
- cc0 (preprocessor and parser) keeps identifiers, macro names included,
  to 255 characters (`NCSYMB` 256 in `h/ops.h`, the buffer `getid` and
  `setid` fill in `n0/lex.c`); longer ones are cut silently. cc1 and cc2
  read names back into buffers of the same size. So a macro name or a
  local, member or static name has 255 significant characters; only
  externals are cut, at l.out. `getid` also stores three NULs after the
  name, which runs two bytes past `id[]` on a name of 255 or more
  characters: keep identifiers well short of that.
- Calling convention: arguments are pushed right to left and the caller pops
  them. The first argument is at `rr14(4)` on entry. Results come back in
  `r1` (16-bit) or `rr0` (32-bit and pointers). `r6`–`r13` are callee-saved
  (compiled code keeps values in `rr6` across calls); `r0`–`r5` are
  scratch. `r13` is the frame pointer and `rr14` the stack
  pointer.
- Long multiply and divide compile inline, so no runtime library is needed.
  The kernel and modules are freestanding; only C programs link the
  [C library](#c-library).

### Long external names

`tools/shortnames.py` (MIT, Python 3 standard library only, no other
Exec900 file needed) writes a header that gives every external over 15
characters a short link name:

```c
#define UnlockPubScreenList UnlockPubSw6p4u
```

A short name is the first 10 characters (`--prefix`) and a 5-digit base-36
hash of the full name, so it is the same on every run and does not depend
on the other names. When it would equal a name of the input set, the
first 15 characters of one, a reserved name or another short name, a salt
is hashed in turn. The macro reaches every token cc0 sees, so the
definition, declarations and calls all agree as long as every file that
uses the name includes the header first; the 15-character limit applies to
the short name only.

- **Names.** A list (`NAMES` files or `-` for stdin, one per line) and/or
  `--scan` of C sources and headers: top-level non-static function and
  variable definitions and extern declarations, K&R or ANSI; statics,
  typedefs, members, parameters and locals are left out. `.globl` names
  of `.s` files are fixed: assembly is not preprocessed by cc0, so they
  are never renamed. The scanner does not expand macros, so a name made by
  a macro must be listed.
- **Reserved.** `--reserved` lists names no short name may take and that
  are never renamed: a name list, or headers and directories (their
  `#define` names and externals), such as Exec900's `include/` and the
  generated `lvo/` headers. A name also reserved and over 15 characters is
  reported and left alone.
- **Stability.** `--map FILE` keeps the assignment across runs: valid
  entries are kept, entries that now collide are renamed (reported), new
  names are added, entries for names that left the input stay reserved,
  and the file is rewritten. Keep it under version control. Without a map,
  adding names renames an existing one only on a hash collision.
- `--collisions-only` renames only long names that agree in their first
  15 characters with another name (fewer renames, but every long external
  still loses its tail at the linker). `--check` reports names that
  truncate to one symbol (exit 1) and warns about each long one (exit 1
  with `--strict`), like `symcheck.py`.

From a separate repository, run it from Exec900's tree and include the
output from the repository's common header, ahead of its prototypes and
LVO headers:

```sh
python3 $EXEC900/tools/shortnames.py --scan src --scan include \
    --reserved $EXEC900/include --reserved $OUT/include/lvo \
    --map shortnames.map --guard WB_SHORTNAMES_H -o include/shortnames.h
```

A name that is a public LVO macro (a program's call) must not be renamed:
programs reach it through the library's jump table, not the linker. Only
the library's own implementation externals need short names. Debugger
and `loutdis` output shows the short names; the map relates them back.
`tests/host/test_shortnames.py` (`make hostcheck`, part of `make check`)
covers the scanner, assignment, the map and the command line.

## Machine model

- Everything runs in segmented system mode in one shared address space,
  matching Exec's single-user model without protection. Tasks are ordinary
  system-mode contexts; the normal-mode stack pointer is unused.
- **Stack segment.** cc2 relocates compiled frame addresses against the
  absolute symbol `SS` (`0x3f3f`), so every Task stack lives in logical
  segment 0x3F. This is the C900 counterpart of the 65816 bank-zero
  constraint: `MEMF_BANK0` (alias `MEMF_STACK`) selects this segment's
  region. Offsets `0xF000`–`0xFFFE` hold the root/boot stack, `0xEE00`–`0xF000`
  the idle stack, and `kstklow`–`0xEE00` the pool for `CreateTask` and
  caller stacks, by default; `kstkcfg.sc_root` moves the root stack's
  low end and the two below it ([root stack
  size](platform.md#root-stack-size)). Offsets below `kstklow` (0x8000–0x8300 for the 32 KB
  segment, 0x0400–0x0700 for 63 KB) have no RAM: the
  descriptor grows down (attribute 0x20) with its limit on the page below,
  so the Z8010's write warning on that page and its length check below it
  trap a stack running off the pool. `STKSEGLOW` and `STKSEGPG` in
  `c900.h` set the 32 KB `crt.s` maps; `loutsize.py` checks them.
  By default `mach_mem()` moves the segment up to 63 KB before any Task
  exists, or to a size patched into the image
  ([size](platform.md#stack-segment-size)). Growing it there rather than
  in `crt.s` keeps the entry code free of memory-map parsing (bootinfo is
  validated in C), and the move is cheap because only the boot stack's
  few live words exist yet. 63, not 64: the base must stay 1 KB-aligned
  and the guard page below `kstklow` needs offsets of its own.
- **Shared stack region.** The pool's MemHeader is ordinary memory of
  the lowest priority, as Amiga chip RAM is: `AllocMem` without
  `MEMF_BANK0` or `MEMF_UPPER` takes it only after the heap and the
  memory handlers have failed, from the top of its highest fitting
  chunk, while stacks are first fit from its bottom, so the two meet
  only when the region is full. Segment 0x3F is one global mapping, so
  such a block is valid in every Task and in interrupt code. This
  replaces a fixed stack-only pool, whose unused bytes were lost to the
  heap and which made every KB of segment past 32 a KB of heap: the
  default had to stay 32 KB on 1 MB, and a Workbench disk booting with
  `--stack=48` gave up 16 KB of heap. Handlers run before the spill
  because only stacks need this region: data placed there while the
  heap still had expungeable libraries would stay when the next stack
  allocation fails, and handlers free only heap. The spill is a second
  `memget` with `MEMF_BANK0|MEMF_REVERSE`, about 20 bytes of text; the
  default size no longer reads the RAM runs, which saves about as much.
- **Layout.** kboot loads text at segment 0x30 (physical 0x080000) and data
  at 0x31. `crt.s` maps segment 0x3F so that its RAM starts at the next
  256-byte boundary after bss: it reads the data descriptor's base back
  from the MMU and adds the linker's `end`. A descriptor base must be
  1 KB-aligned, so the base is the 1 KB boundary at or below that start,
  less `STKSEGPG`; the segment then ends, and the heap starts, 1 KB-aligned,
  and the up to 768 bytes between are stack pool rather than a gap. A whole
  page for the stack segment would strand the rest of the image's last
  page, 64 KB on the default image; packing it leaves no gap, and a
  growing image moves the heap start by 1 KB steps instead of a page. The price is a stack segment
  that lies across a physical page boundary, which only DMA sees
  ([wd.device](#wddevice)). The [memory map](platform.md#memory-map) is the
  contract; `mach_mem()` builds it from kboot's bootinfo runs:
  - RAM below physical 0x40 and past the stack segment is mapped
    **identity** (logical = physical), one `MEMF_PUBLIC|MEMF_UPPER`
    MemHeader per 64 KB page from offset 0; the page the stack segment ends
    in is a MemHeader from that end up (`HeapTail` bytes; `HeapPages`
    counts whole pages) and holds every other MemHeader. RAM without an identity segment takes a
    relocation slot (0x04, 0x05, 0x07, 0x08, and 0x09 when the heap starts
    above it): physical 0x30 and 0x31, whose numbers are the kernel's
    segments, then RAM above 0x3F. `mh_Upper` stores the exclusive end
    **offset** (up to 0x10000) within `mh_Lower`'s segment.
  - RAM the slots cannot hold goes to a **physical-only pool** of pages
    (`kallocphys`/`kfreephys`) reached through two **window** segments
    (`kwinmap`); the one Z8010 has 64 descriptors and never translates
    segments 0x40–0x7F.
  - `kmmutab[]` records every descriptor's base so `kphys()` can give DMA
    code a physical address; `kmapseg()` is the only writer after boot.
  - Every descriptor without a mapping, segment 0 included, is CPU-inhibit,
    so NULL and stray-segment accesses take the segment trap.

  **Departure from Amiga Exec.** Exec on the Amiga sees one flat address
  space, and every `AddMemList` region is CPU-addressable memory. Here
  memory the MMU cannot map at once (what is left above physical 0x3F once
  every spare descriptor holds a page) is
  not given to the allocator at all, because an `AllocMem` result must be a
  plain pointer valid in every context. It is a private page pool instead,
  for kernel users that can work through a window (RAM disk, disk cache).
  Identity mapping is chosen over packing pages from segment 0x02 so that a
  pointer names its physical page for MMU-bypassing DMA. The boot ROM's state
  block is not preserved: its configuration is copied before segment 1 is
  inhibited and the ROM is never called again.
- **Trap frame** (36 bytes, offsets from SP, `struct TrapFrame`): `+0`
  r0..r13, `+28` identifier, `+30` FCW, `+32` PC segment word, `+34` PC
  offset. Every entry (`SC`, interrupts, traps) builds this frame on the
  interrupted stack and passes its **offset** to C. A handler resumes
  whatever offset C returns, so returning another Task's frame offset is a
  context switch. Traps go through `kfault` to the Task's `tc_TrapCode`
  and resume their own frame ([traps](platform.md#traps)). `tc_SPReg` holds the
  saved frame as a full pointer (segment 0x3F).
- **New Task stack.** At the top of the stack, 4 bytes hold the finalizer
  address. This is the entry's return address: a returning entry "returns"
  into finalPC, or into `kfinal` when finalPC is NULL, and `kfinal` calls
  `RemTask(NULL)`. Below that is a 36-byte frame with zeroed registers,
  identifier 0, FCW `0xD800` (segmented, system, VI and NVI enabled) and PC
  set to the entry.
- **Reschedule gate.** `kresched()` executes `sc $0`. The SC entry calls
  `UWORD kswitch(UWORD frameoff)` with VI off. Callers turn VI off with
  `s = intoff()` around any queue change and call `kresched()` while it is
  still off; the resumed Task's own FCW comes back on IRET. Then they call
  `intrest(s)`.
- **Interrupt mask.** `intoff()` turns off VI and NVI together and returns
  the old FCW; `intrest(s)` restores each enable from `s`. `Disable` masks
  both. Every PSA entry's FCW is `0xC000`, so handlers run with both off and
  interrupts never nest. NMI is tied off.
- **Tick.** Z8036 CIO #1 counter/timer 3 runs at 100 Hz on vector 0. Its
  first server acknowledges the chip and calls `ktick()` (time, sleepers,
  quantum); see [Interrupts](#interrupts).
- **Console.** Polled SCC: status `inb(0x0101)`, bit 2 = Tx empty and bit 0
  = Rx available; data at `0x0111`. `con_init` programs it (9600 8N1).
  It shares line 0 with serial.device unit 0; see
  [serial.device](#serialdevice). A display console gets every byte too;
  see [Console](#console).
- **Emulator stop.** `emustop()` writes `0xC900` to port `0x0FFE`. The
  emulator's `--stop-on=port` ends the run there; hardware ignores the write.

## Beyond one stack segment

Design note only; nothing here is implemented. The 63 KB ceiling comes
from cc2: every frame address is relocated against the absolute `SS`
(`0x3f3f`), so a stack's segment number is a compile-time constant and
every stack, the interrupt stack included, must be reached as segment
0x3F. Two ways past it:

- **Remap segment 0x3F per Task.** Each Task gets its own stack region
  (1 KB-granular, up to 63 KB, from the heap, 1 KB-aligned) and the
  switch reloads descriptor 0x3F (select, base high, base low, limit,
  attribute: five `soutb`, about 60 clocks, plus the `kmmutab` entry
  `kphys` reads). The reload cannot be in `kswitch`, which runs on the
  outgoing stack and returns through it: it goes in `trap.s` between
  `ld r15, r1` and `ldm`, with the new base and limit returned in
  registers. Interrupts make it harder: `kintr` runs on the interrupt
  stack, which is in segment 0x3F, so either every region reserves the
  interrupt stack at one fixed offset (512 bytes per Task) or interrupt
  entry and exit each reload the descriptor to a shared interrupt region
  (about 120 clocks per interrupt). The real cost is semantic: a stack
  address means a different byte in every other context. Amiga code
  passes stack objects across contexts all the time: a local
  `IOStdReq` given to `DoIO`, a reply port or `SignalSemaphore` on the
  stack, a buffer a driver fills at interrupt time, `StackSwap` onto
  another Task's memory. Each would read or write the running Task's
  region instead. It would need a rule (no stack object reached by
  another Task or by interrupt code) that classic Amiga software
  breaks, so it fits only Tasks known to follow it, alongside a shared
  segment for the rest, which one descriptor cannot provide.
- **Per-Task segment numbers.** With cc2 relocating frame addresses
  against the stack pointer's own segment (the segment word of `rr14`)
  instead of `SS`, each Task could have a distinct segment and pointers
  would stay valid everywhere, with no work at a switch. It needs that
  toolchain change and free logical segments: on a 1 MB machine
  0x18–0x2F, 0x32–0x36 and 0x38–0x3D are unused (35), but on 3 MB and larger
  machines the identity heap takes 0x0A–0x3D and the relocation slots
  the rest, so stack segments would come out of heap identity mappings
  (each costs a 64 KB logical segment, not its RAM).

Until either exists, the planning number is 63 KB of stack in total,
about 58 KB of it for Task stacks after the root, idle and interrupt
stacks; `C:StackUse` shows how much each Task really needs.

## Scheduling and synchronisation

These follow classic Amiga Exec, which `AGENTS.md` names as the default model.
Departures from the upstream 65816 build are deliberate:

- The ready queue is priority-ordered with `Enqueue`, round robin within a
  priority, and a quantum of 4 ticks. Upstream ignores priority as a
  temporary simplification; `SetTaskPri` here reorders the ready queue as
  classic Exec does.
- Public `Disable`/`Enable` are implemented, as on the Amiga. They turn VI
  and NVI off and on together (`intoff`, `di VI, NVI`; `Enable` restores
  `FCW_TASK`) with a per-Task `tc_IDNestCnt` (−1 = enabled). There is no
  call that masks NVI alone. A Task that Waits while Disabled resumes
  with both still off, because its FCW is in its frame. The
  [VI-off budget](#vi-off-budget) bounds how long.
- `Forbid`/`Permit` use a per-Task `tc_TDNestCnt` (−1 = permitted). The tick
  and `kmaybe()` switch only when the current Task has both counts below 0
  and `IntDepth == 0`; otherwise they leave `SF_SAR` pending for `Permit`,
  `Enable` or the next tick. They are hand-written in `platform/semfast.s`
  (no frame, no saved registers); the nesting checks panic through
  `knestbad`.
- Kernel services are ordinary C calls in the caller's context, as in classic
  Exec. They do not go through a COP-style dispatch gateway. The `SC` trap
  exists only for the switch; the 65816 gateway responds to bank-zero and
  direct-page constraints that this machine doesn't have.
- The idle Task (private, priority −128, never queued) runs when
  `TaskReady` is empty and executes `halt` in a loop with VI enabled.
- Boot adopts the boot context with `kinitroot()`, then prepares the idle
  Task with `kinitidle()`.
- `Sleep(ticks)` is the timed-delay extension; under Forbid it blocks like
  Wait (upstream returns ERROR_LOCK instead). It waits on private
  `SIGF_SLEEP` with a deadline in `TC_WAKE(t)`; the tick wakes due sleepers.
- `struct Task` carries Amiga's `tc_TrapData` and `tc_TrapCode`, in
  Amiga's place after `tc_SigExcept`. Upstream's `abi/tasks.json` has
  neither, so `tools/genabi.py` inserts them from its `EXTRA` table, which
  names the upstream field each C900 field follows; the generator stops if
  that field disappears. Other Amiga fields are added the same way.
- `RemTask` frees every MemList on `tc_MemEntry` with `FreeEntry`, as
  Amiga Exec does, so `AddTask` callers `NewList` it first. `CreateTask` is
  amiga.lib's: one `AllocEntry` list holds the Task record
  (`MEMF_PUBLIC|MEMF_CLEAR`) and the stack (`MEMF_STACK|MEMF_CLEAR`, size
  rounded up to 4) and goes on `tc_MemEntry`; it checks no arguments beyond
  what `AddTask` refuses. A Task removing itself takes it off every list,
  frees the lists under Forbid and keeps running on the freed record and
  stack until the switch. That is safe because Deallocate writes only the
  first 8 bytes of a freed block, the dying frame is at the top of its
  stack, the Task fields still read lie past those 8 bytes, and Forbid keeps
  every allocator out until the switch.
- The allocator serialises with `Forbid`/`Permit`, never from interrupts.
- Signal bits 0–15 are system bits and start allocated (`tc_SigAlloc =
  0x0000FFFF`). `AllocSignal(-1)` picks the highest free bit; failure returns
  −1.
- `FreeSignal` follows Amiga Exec (V37+), not upstream's misuse check: a
  low byte of 0xFF is ignored, so `DeleteMsgPort` accepts a port whose
  `mp_SigBit` (UBYTE) was set to −1 after its bit was given away, as
  amiga.lib's `TimeDelay` does. Any other value clears bit `n` modulo 32
  with no diagnostic: freeing a system bit or an unallocated bit is not an
  alert on AmigaOS either. `AllocSignal` still refuses bits 0–15.

## Signal semaphores

Upstream has no semaphores. `kernel/semaphores.c` follows Amiga Exec V39,
with the V39 records in `include/exec/semaphores.h` (`SignalSemaphore`,
`SemaphoreRequest`, `SemaphoreMessage`, `NT_SIGNALSEM` = 15). The calls are
InitSemaphore, ObtainSemaphore, ObtainSemaphoreShared, AttemptSemaphore,
AttemptSemaphoreShared, ReleaseSemaphore, ObtainSemaphoreList,
ReleaseSemaphoreList, AddSemaphore, FindSemaphore, RemSemaphore, Procure and
Vacate. Their link names are shortened (`xObtainSem`, `xObtainSemShr`,
`xAttemptSem`, `xAttemptSemShr`, `xReleaseSem`, `xObtainSemList`,
`xReleaseSemList`).

- Every queueing or granting step runs under Forbid; no call may be made
  from an interrupt. ObtainSemaphore, ObtainSemaphoreShared,
  AttemptSemaphore, AttemptSemaphoreShared and ReleaseSemaphore have an
  uncontended path in `platform/semfast.s`: the same count test and update
  with VI and NVI off for about a dozen instructions instead of
  Forbid/Permit, then the caller's FCW back. It covers a free semaphore, a
  nested or shared hold, an Attempt (taken or refused) and a release that
  leaves the semaphore held or frees it with nothing queued. A busy
  Obtain, a last release with requests queued, or a bad release leaves the
  semaphore untouched and goes to the C call, which decides again under
  Forbid. The fast path cannot interleave with a queueing step, since
  Forbid keeps other Tasks off the CPU and the fast path keeps the tick
  out.
- `ss_QueueCount` is −1 when free and counts every hold and every queued
  request, less one. `ss_NestCount` counts holds. `ss_Owner` is the
  exclusive holder; it is NULL when the semaphore is free or held shared.
- The exclusive owner may obtain again, exclusively or shared; each obtain
  needs a release. A shared request is granted at once while the semaphore
  is shared, even with requests queued. A shared holder asking for
  exclusive access deadlocks, as on the Amiga.
- A Task that cannot have the semaphore queues a `SemaphoreRequest` on its
  own stack at the tail of `ss_WaitQueue`, clears `SIGF_SINGLE` and Waits
  for it. Bit 0 of `sr_Waiter` marks a shared request. Wait under Forbid
  switches Tasks because Forbid is the per-Task `tc_TDNestCnt`; the Task
  resumes still forbidden.
- The last release grants the head request. An exclusive head request
  becomes `ss_Owner`; a shared one is granted with every shared request
  directly behind it. The releaser counts each grant and sets the owner
  before signalling, so a waiter returns holding the semaphore. Grants are
  in queue order, not by priority.
- ReleaseSemaphore on a semaphore that is free, or held exclusively by
  another Task, raises the recoverable alert `AN_SemCorrupt` (0x01000008).
  The kernel raises it directly, under `Forbid`, so it is console text
  only (`exec900: alert 01000008`), never intuition's display; it
  returns with the semaphore unchanged. A shared release cannot be checked
  against its holder.
- ObtainSemaphoreList obtains every semaphore on a list linked through
  `ss_Link`, exclusively. Busy semaphores queue their `ss_MultipleLink`, so
  only one Task at a time may obtain a given list.
- AddSemaphore initialises the semaphore and enqueues it by priority on the
  system `SemaphoreList`; FindSemaphore and RemSemaphore take Forbid
  themselves.
- Procure bids with a `SemaphoreMessage` whose `mn_Node.ln_Name` is
  `SM_SHARED` or `SM_EXCLUSIVE`. A grant replies the message with
  `ssm_Semaphore` set. A queued bid clears `sr_Waiter`, which overlays
  `ln_Type`, `ln_Pri` and the high word of `ln_Name`, so a NULL waiter marks
  a message. An exclusive bid granted later makes the reply port's
  `mp_SigTask` the owner. Vacate withdraws a queued bid, replying it with
  `ssm_Semaphore` NULL, or releases a granted one.

## Interrupts

Upstream has no interrupt interface: its VBI and SIO entries are fixed
native code and a fixed table of deferred native sources, with "no
application callback registration interface". Drivers here need one, so
Exec900 follows classic Amiga Exec (`<exec/interrupts.h>`):

```c
struct Interrupt { struct Node is_Node; APTR is_Data; int (*is_Code)(); };
struct Interrupt *SetIntVector(UWORD intnum, struct Interrupt *is); /* old */
void AddIntServer(UWORD intnum, struct Interrupt *is);
void RemIntServer(UWORD intnum, struct Interrupt *is);
ULONG IntStray(WORD intnum);          /* unclaimed count; INTV_ALL totals */
```

- **Numbers.** An interrupt number is the even vector the chip supplies
  (`0x00`–`0xFE`, `INTV_TICK`, `INTV_SCC`, `INTV_DISK`, …), or `INTV_NVI`
  (`0x100`). Odd vectors are unsupported: the Z8001 indexes the PSA with the
  raw identifier, so an odd one lands between two PSA entries. Any other
  number panics. This replaces Amiga's 15 Paula interrupt bits.
- **Dispatch.** Every PSA vector entry and the NVI entry go to one stub,
  `tvi_`/`tnvi_`, which saves the trap frame on the interrupted stack, moves
  to the interrupt stack and calls `UWORD kintr(UWORD frameoff, UWORD id)`.
  A number has a handler (`SetIntVector`) or a server chain
  (`AddIntServer`, priority order, FIFO among equals). A handler replaces
  the chain while it is set. Handlers and servers are called as
  `code(is_Data, intnum)`; a server returns nonzero when the interrupt was
  its own, which ends the chain, and zero to pass it on. Servers sharing a
  vector must all return zero (the tick chain does).
- **Interrupt stack.** C dispatch, every handler and the exit switch run on
  one 512-byte stack allocated from `MEMF_STACK` at boot (`kintstk`, top
  offset). Handlers don't nest, so one is enough. A Task stack carries only
  the 36-byte frame for an interrupt. The deepest measured dispatch, the
  timer server replying to a waiting Task, used 260 bytes.
- **Exit.** `kintr` counts `IntDepth` around dispatch. On exit it calls
  `kswitch` when `SF_SAR` is set, `IntDepth` is 0 and the interrupted Task
  has `Forbid` and `Disable` counts below 0; otherwise the switch stays
  pending for `Permit`, `Enable` or the next interrupt. So any handler that
  Signals, PutMsgs or ReplyMsgs a Task of higher priority switches to it on
  the same interrupt's exit. Inside a handler `Disable`/`Enable` nest but
  leave VI off (`Enable` turns VI on only at `IntDepth` 0), and
  `Forbid`/`Permit` never switch.
- **The tick.** At boot `kintinit` adds the scheduler's server to
  `INTV_TICK` at priority 127 and `timer.device` adds its server at 100.
  The first acknowledges the CIO (clear IP and IUS); a tick handler set with
  `SetIntVector(INTV_TICK, …)` must acknowledge it itself, and replaces the
  scheduler tick while set.
- **Unclaimed interrupts** (no handler and an empty chain, or an NVI no
  server claimed) return at once and are counted per number. More than 1000
  between two ticks panics as an interrupt storm: a source with no handler
  is holding its line. kboot's polled disk reads leave one `INTV_DISK`
  completion latched; it arrives when the tick enables VI, and
  `wd.device`'s handler dismisses it (`WdStray`). The root-exit transcript
  lists the counts.
- **NVI** carries single-step and the LC card's 8568 INTR, which the 8568
  asserts from power-up and on every Update Ready rise until its status
  port `0x0181` is read. `kintinit` reads it once before interrupts are
  enabled, and `kintr` reads it on every NVI before the handler or chain
  runs, so a single-step handler that claims every NVI cannot leave the
  line held. The read does not disturb the 8568's register index. An NVI
  nothing claims is then counted as stray. The LC backend leaves no INTR
  pending ([LC display](#lc-display)), so with it the NVI stays quiet. NVI
  is enabled in every Task FCW (`FCW_TASK` `0xD800`). The emulator never
  raises NVI.

**Writing a handler.** Install before enabling the chip's interrupt, and
disable the chip before removing it. Acknowledge the chip in the handler
(SCC: Reset IUS; CIO: clear IP/IUS). Don't call anything that waits or
allocates memory. Signal, PutMsg, ReplyMsg, `TermIO`, `AddIntServer` and
list operations are safe. Data shared with Task code is changed in the Task
under `Disable` (or `intoff`/`intrest`), because `Forbid` doesn't hold off
interrupts. A server may remove itself; no other chain edits from inside a
chain.

### VI-off budget

`Disable`, `intoff` and every handler mask VI and NVI together; nothing
masks NVI alone. A section with them off should last **at most 1 ms**,
counting its ready waits. The limits behind the number:

| Source | Holds | Lost after |
| --- | --- | --- |
| SCC receive at 9600 / 19200 / 38400 baud | 3-byte FIFO, 1.04 / 0.52 / 0.26 ms a byte | about 3.1 / 1.6 / 0.8 ms |
| 100 Hz tick (CIO CT3) | one pending tick | 10 ms (a tick lost; time drifts) |
| Serial mouse, 1200 baud | 3 bytes, 8.3 ms each | 25 ms |
| Disk (`wd.device`) | DMA completion stays latched | never; only latency (timeouts are 3 s and more) |
| LANCE receive | the chip's ring | ring full |

1 ms leaves 19200 baud input a margin for the handler that drains it
and for one more section just before. The worst sections now: an LC
8568 block piece (255 bytes, about 1 ms), lance.device's frame copy in
its server (1-1.5 ms, so above 9600 a full frame can overrun), and a
font glyph at LC reset (8 data writes and a seek, an estimated
0.1-0.2 ms). A longer job runs in
pieces with interrupts let in between, as the LC backend does.

## Z8070 EPU

The contract is [Z8070 EPU](platform.md#z8070-epu); `kernel/epu.c` and
`platform/epuasm.s` implement it.

- **Model.** Amiga Exec saves the 68881 at every switch (`FSAVE`, and the
  registers when the frame is not null) on the Task's stack. The Z8070
  has no null frame, so an eager save would cost every switch of an EPU
  Task a full save and load. The Z8001's FCW EPA bit gives a lazy switch
  instead: with EPA clear an extended instruction traps before the EPU
  sees more than its first word. Tasks run with EPA clear (`FCW_TASK`);
  only `EpuOwner` has it set in its FCW, which lives in its frame while
  it is switched out. `kswitch` is unchanged.
- **The trap.** `kfault` hands trap kind 1 to `kepuswitch`, which acts
  when `AFF_68881` is set, `IntDepth` is 0 and the word before the saved
  PC is the identifier (the CPU saves the address of the second word).
  It finds or allocates the Task's save area, saves the owner's state
  into the owner's area (`kepusave`), clears EPA in the owner's frame at
  `tc_SPReg`, loads the Task's state (`kepuload`), sets EPA in the frame
  and backs the PC up 2 bytes. The allocation runs with `IntDepth`
  raised, so its `Permit` cannot switch inside the trap. `RemTask`
  clears `EpuOwner` before freeing a dying owner's lists.
- **Save area.** Heap, not stack: the state must outlive the switch
  (the owner keeps running on its stack until it next needs the EPU),
  and segment 0x3F is the scarce memory. 88 bytes: F0–F7 as 5-word
  extended values, FFLAGS, USER, a pad word. FFLAGS is loaded after the
  registers, since loading them can set flags.
- **The EPU sees the trap.** The Z8070 snoops the bus: it takes the
  extended instruction's first word (status 1101) and then the next
  program-space read (1100) as its second word. In a trap that read is
  the PSA entry's FCW. The extended-instruction entry's FCW is `0xC0F0`:
  its flag bits make the operation field 0x3F, an invalid opcode for
  every template, which the EPU drops (SC.IR and SC.IP record it; MIE
  stays 0, so no interrupt). The trap's aborted fetch at the second word
  would look like a new first word only for a CPU register field of R14
  or R15. Source: the core's `z8070_ip` and sequencer.
- **Probe.** `kepuprobe` runs before the tick with EPA set. The C
  emulator traps every extended instruction even with EPA set (and saves
  the PC past both words), so `kepurec` holds a resume address while the
  probe runs and `kfault` sends the trap there. The Go model has no EPU
  and the probe finds none.
- **Departures.** Lazy switching through the EPA trap instead of a save
  at every switch, and a heap save area instead of the Task's stack;
  both for cost on a 6 MHz CPU and the stack segment's size. SC is
  global: it holds no per-Task state Exec uses.
- **Cost.** `kswitch` is unchanged, so a switch costs the same with and
  without an EPU. A move of the EPU between Tasks is one trap, 16 FLD/FST
  extended transfers (8 each way, 11 + 3 × 5 CPU clocks apiece plus EPU
  time) and 4 FLDCTL transfers: about 800–1,000 clocks, estimated, not
  yet measured on the core bench. Kernel text grew 800 bytes, data and
  bss 36 bytes each.
- **Measured** (Go model, 6 MHz, `tests/epu` without an EPU): 300
  software double adds take 4–5 ticks (about 900 clocks each), multiplies
  14 (2,800), divides 19–20 (3,900); a Signal/Wait round trip between two
  Tasks, two switches, takes 1.33 ms (8,000 clocks). The EPU timings in
  `tests/epu` print only with `tb_core +fpu`.

## Device I/O

The model is upstream's ([device I/O](../../docs/reference/device-io.md),
[resident drivers](../../docs/reference/resident-drivers.md)), with
`IORequest`, `IOStdReq`, `CMD_*`, `IOF_QUICK` and `IOERR_*` generated from
`abi/io.json`. Calls (`<exec/exec.h>`):

```c
void AddDevice(struct Device *dev);
APTR RemDevice(struct Device *dev);   /* the device's Expunge result */
BYTE OpenDevice(char *name, ULONG unit, struct IORequest *ior, ULONG flags);
void CloseDevice(struct IORequest *ior);
void BeginIO(struct IORequest *ior);  /* macro: io_Flags as given */
void SendIO(struct IORequest *ior);   /* io_Flags = 0 */
BYTE DoIO(struct IORequest *ior);     /* io_Flags = IOF_QUICK; waits */
struct IORequest *CheckIO(struct IORequest *ior); /* NULL while pending */
BYTE WaitIO(struct IORequest *ior);
LONG AbortIO(struct IORequest *ior);  /* driver result, 0 = aborted */
void TermIO(struct IORequest *ior);   /* driver completion */
struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size);
void DeleteIORequest(struct IORequest *ior);
```

- **Devices** are Amiga libraries (`<exec/devices.h>`,
  [libraries](platform.md#libraries-and-sysbase)): `struct Device` is a
  `struct Library` whose jump table holds the entries below, each called
  with the device base as its first argument. A resident driver keeps its
  base in a static record after six vectors and fills them with
  `MakeFunctions` (`kinitlib`); `gameport.device`, a boot module,
  comes from `MakeLibrary`. `AddDevice` sums the vectors and
  lists the device by priority; `OpenDevice` finds it by name.

  | LVO | Called as | Contract |
  | --- | --- | --- |
  | −6 Open | `(dev, ior, ULONG unit, ULONG flags)` | Check unit and flags; on failure set `io_Error`; on success set `io_Unit`, count `lib_OpenCnt` and clear `LIBF_DELEXP`. Exec has set `io_Device` and cleared `io_Error`. |
  | −12 Close | `(dev, ior)`: seglist | Release the unit reference, uncount `lib_OpenCnt`; the last close of a `LIBF_DELEXP` device expunges. Exec then clears `io_Device`/`io_Unit`. |
  | −18 Expunge | `(dev)`: seglist | With opens, set `LIBF_DELEXP` and return NULL; otherwise unlist and free the device. Residents return NULL and stay. |
  | −24 | reserved | Returns 0. |
  | −30 BeginIO | `(dev, ior)` | Clear `io_Error` and mark the request `NT_MESSAGE`; complete at once with `TermIO`, or clear `IOF_QUICK`, queue, and complete later exactly once. |
  | −36 AbortIO | `(dev, ior)`: `LONG` | If still queued, unlink, set `IOERR_ABORTED`, `ReplyMsg` and return 0; otherwise return −1. |

  As on the Amiga, OpenDevice and CloseDevice call Open and Close under
  `Forbid`, and RemDevice calls Expunge under Forbid; SendIO, DoIO,
  BeginIO and AbortIO call the vectors directly in the caller's Task, with
  no lock and interrupts enabled. None of the vectors may Wait.
- **Driver exclusion** is each driver's own, as on the Amiga. State the
  driver's interrupt handlers also touch (queues, rings, unit state) is
  read and changed under `Disable` (`intoff`); state shared only between
  Tasks, such as a queue a driver Task serves or a unit reprogrammed
  outside `Disable`, under `Forbid`. A request must not become visible to
  a handler while `IOF_QUICK` is still set and the driver means to queue
  it: the driver clears the flag first, or under the same `Disable`.
- **Submission.** SendIO sets `io_Flags` to 0 and DoIO to `IOF_QUICK`;
  both clear `io_Error`, mark the request `NT_MESSAGE` (pending) and call
  the device's −30 vector. `BeginIO` is amiga.lib's, a macro in
  `<exec/devices.h>` that calls −30 with `io_Flags` as given, so each
  driver's BeginIO starts with the same marking, as the Amiga's do. A quick
  completion keeps `IOF_QUICK`, sends no reply and leaves the request
  `NT_MESSAGE`; any other completion is one `ReplyMsg` (`NT_REPLYMSG`).
  `TermIO` does whichever `IOF_QUICK` asks for and is safe in a handler.
- **Collection** is Amiga's algorithm. `CheckIO` returns the request when
  `IOF_QUICK` is set or its type is `NT_REPLYMSG`, else NULL.
  `CreateIORequest` makes a request `NT_REPLYMSG`, as amiga.lib's
  `CreateExtIO` does, so one never sent counts as done. `WaitIO` returns
  `io_Error` at once for a quick request; otherwise it waits on the reply
  port's signal, under `intoff`, until the type is `NT_REPLYMSG`, then
  removes the request from the port. Other messages stay; the signal they
  raised may be consumed, as on the Amiga (`WaitPort` and `GetMsg` look at
  the list first). WaitIO of a request never sent, or one already taken
  with `GetMsg`, is misuse, as on the Amiga. DoIO is BeginIO with
  `IOF_QUICK` and then WaitIO. DoIO and WaitIO need a `PA_SIGNAL` reply
  port of the calling Task.
- **Completing from an interrupt.** Unlink the request from the driver's
  queue, set `io_Error` (and `io_Actual`), then `ReplyMsg(&ior->io_Message)`
  or `TermIO(ior)`. A reply that readies a higher-priority Task switches on
  the interrupt's exit.
- **Lifetime** is Amiga's: RemDevice calls the device's Expunge, which
  unlists and frees an unopened device or sets `LIBF_DELEXP`; the device
  stays listed and a new Open clears the flag. The last Close of a
  flagged device expunges it. `CreateIORequest` refuses a NULL port and sizes outside
  `sizeof(struct Message)`–65,535, and stores the size in `mn_Length`;
  `DeleteIORequest` frees `mn_Length` bytes.
- **Resident drivers** are compiled into the image. Each has a
  `struct Resident` romtag (`RTF_COLDSTART`, not `RTF_AUTOINIT`) whose
  `rt_Init` is its init function; `ResModules` (`kernel/devices.c`) lists
  them in initialisation order, and boot runs `InitCode(RTF_COLDSTART, 0)`
  after the interrupt and memory setup and before the tick starts. A
  loaded driver would use `InitResident` or `MakeLibrary` and `AddDevice`;
  nothing in the model depends on the image.

Departures from upstream and Amiga, and why:

- Upstream routes OpenDevice through a gateway to a static, generated route
  table; arbitrary runtime registration is unsupported. Here `AddDevice` and
  a priority-ordered list (Amiga's model) let drivers in separate files and,
  later, loaded drivers register without editing a central table.
- The device base is a hidden first argument instead of A6: MWC has no
  register calling convention.
- Amiga's CloseDevice leaves `io_Device` and `io_Unit` at −1 to catch
  reuse; an access through −1 (segment 0x7F) does not trap on the Z8010,
  so they are set to NULL, which does.
- `struct Unit` keeps Amiga's layout and is optional.
  `io_Device` and `io_Unit` are typed `struct Device *` and
  `struct Unit *` (upstream: untyped byte pointers); `tools/genabi.py`
  maps them.
- `OpenDevice`, `DoIO` and `WaitIO` return `BYTE` and `AbortIO` `LONG`,
  as on the Amiga (upstream: `int` and nothing).
- Upstream's caller-side wrapper holds `Forbid` around submission and
  AbortIO. Here SendIO, DoIO, BeginIO and AbortIO call the driver
  directly, as Amiga Exec does, and each driver locks only what it shares:
  most need just `Disable` against their handlers, and the hot path pays
  no Forbid.
- `TermIO` is an Exec900 helper, the completion every Amiga driver writes by
  hand and upstream drivers keep private.
- No upstream Task leases (`RetainTask`) exist here, so drivers don't retain
  the opener; a driver must not outlive its clients' requests.
- The `MISUSE` and test selectors of `abi/io.json` are 65816 gateway
  details and are not used.

## timer.device

The AmigaOS 3.x timer.device (`<devices/timer.h>`, `<lvo/timer_lib.h>`
from `fd/timer_lib.fd`) on the 100 Hz tick. Upstream's interface
(`abi/timer-device.json`: `TD_READCLOCK`, `TD_WAITUNTIL`, `TimerRequest`,
`TimerClockRequest`, the `TIMERERR_` codes) is not used: its command
numbers 10 and 11 are Amiga's `TR_GETSYSTIME` and `TR_SETSYSTIME`.
`ReadEClock` and `UNIT_WAITECLOCK` cover its tick clock and absolute wait.

- **Units.** `UNIT_MICROHZ`, `UNIT_VBLANK`, `UNIT_ECLOCK`,
  `UNIT_WAITUNTIL` and `UNIT_WAITECLOCK`, each with a `struct Unit`; other
  numbers fail with `IOERR_OPENFAIL`. Flags are ignored and no reply port
  is needed, so `OpenDevice` on a bare `timerequest` gives `TimerBase`.
- **E-clock.** C/T 3's input: `ReadEClock` returns its rate, the time
  constant times 100 (3,000,000 at 6 MHz, 2,000,000 at 4 MHz), and fills
  a 64-bit count. The tick server adds the time constant at each tick;
  between ticks the current count adds the counts since. The count is
  frozen (RCC) and read with VI off. A tick the scheduler's server has
  taken but this one has not yet, or a pending one (IP set and the count
  reloaded, less than half a tick ago), adds a tick.
- **Live count.** At the first tick the server reads the count twice;
  unless both reads are within the time constant and differ, the count
  is taken as not readable (the C emulator reads 0) and every clock read
  stays on the tick: 10 ms resolution. The Go model and the core read it.
- **System time.** `GetSysTime` and `TR_GETSYSTIME`: seconds and
  microseconds since 1978-01-01, the time at the last tick plus the
  E-clock counts since, divided by counts per microsecond. At boot it
  starts at `ReadBattClock()` (0 with no clock): `ResModules` initialises
  battclock.resource first. `TR_SETSYSTIME` sets it; the battery clock is
  not written. Both complete quick.
- **Delays.** `TR_ADDREQUEST` on `UNIT_MICROHZ` and `UNIT_VBLANK` converts
  `tr_time` (microseconds of a million or more carry into seconds) to
  E-clock counts, on `UNIT_ECLOCK` takes `tr_time` as a `struct
  EClockVal`, and adds the E-clock now; without a live count, now is
  taken one tick late. `UNIT_WAITECLOCK` takes `tr_time` as an absolute
  E-clock and `UNIT_WAITUNTIL` as an absolute system time; a target
  already reached completes at once (quick under `DoIO`). The pending
  E-clock requests form one list and the `UNIT_WAITUNTIL` requests another,
  each ordered by deadline, FIFO among equal deadlines; each tick replies
  to every request whose deadline the tick's E-clock or system time has
  reached, so no delay ends early and a delay ends within the tick
  after its deadline. `TR_SETSYSTIME` moves `UNIT_WAITUNTIL` deadlines
  with the clock; the others do not move.
- **Fields.** While pending, a relative request's `tr_time` holds its
  E-clock deadline; on completion it is cleared, as upstream clears its
  fields. Absolute requests keep `tr_time`. After `AbortIO`, a relative
  request's `tr_time` is undefined.
- **AbortIO** of a pending request replies `IOERR_ABORTED` and returns 0;
  of a finished one, it does nothing and returns -1. CloseDevice with a
  request still pending on the same reply port panics.
- **Base functions** at Amiga's LVOs: `AddTime` (−42), `SubTime`,
  `CmpTime`, `ReadEClock`, `GetSysTime` (−66), each called with
  `TimerBase` first. `AddTime` and `SubTime` take normalised values and
  wrap: a negative difference has `tv_secs` above 0x7FFFFFFF.
  `CmpTime(a, b)` is 0, −1 when `a` is later and 1 when earlier. The clock
  reads may be called from interrupt code.
- **Cost.** The tick server adds a 64-bit E-clock step, a system-time step
  and two list-head checks to each tick. A clock read with a live count is
  three port reads and one write with VI off, then one division.

Tests: `tests/timer.c` (E-clock, delays on each relative unit,
`UNIT_WAITECLOCK`, ordering, AbortIO) and `tests/systime.c` (seed, system
time rate and order, the base functions, `TR_SETSYSTIME`,
`UNIT_WAITUNTIL`); `systime` prints which resolution it found.

Departures: the resolution is the tick on any machine without a readable
count, where a 1 µs `UNIT_MICROHZ` delay can last two ticks. A delay ends
at a tick, never between, so even with a live count `UNIT_MICROHZ` is no
finer than `UNIT_VBLANK`: a short delay ends at the next tick. Amiga's
E-clock is 709,379 or 715,909 Hz; programs must use `ReadEClock`'s rate.
If VI stays off for more than half a tick, a read in that window can be
a tick early. Upstream's fixed tables of 8 opens and 16 pending requests
and its deferred completion budget are not needed: requests link through
their own message node and the server completes all due requests in the
tick interrupt. `Sleep` keeps its own mechanism.

## battclock

Upstream has no wall clock: timer.md leaves wall-clock time unsupported and
`TD_READCLOCK` counts ticks. Exec900 follows Amiga's `battclock.resource`
(`<resources/battclock.h>`), driving the MSM58321 on CIO U70:

```c
APTR BattClockBase = OpenResource(BATTCLOCKNAME);
ULONG ReadBattClock();         /* seconds since 1978-01-01 00:00:00; 0: no clock */
int   WriteBattClock(ULONG t); /* 0, BCERR_NOCLOCK, BCERR_RANGE (t > BC_MAXTIME) */
int   ResetBattClock();        /* WriteBattClock(0) */
```

The calls are the resource's vectors −6 (Reset), −12 (Read) and −18
(Write), generated from `fd/battclock_lib.fd`; the program defines
`BattClockBase`, as on the Amiga.

- **Range.** The chip keeps a two-digit year: 78–99 are 19yy, 00–77 20yy,
  so 1978-01-01 to 2077-12-31 23:59:59 (`BC_MAXTIME`). The one century year
  in range, 2000, is leap, so `year % 4` is exact. After 2077 the clock
  reads as 1978.
- **Read.** The chip's BUSY output is not wired, so a carry can land
  between register reads. A read takes all 13 time registers twice with
  /CS held and accepts them only when both passes agree, up to 4 tries;
  `BattRetries` counts torn pairs. The image must then be a valid date and
  time (BCD digits, hour in the mode H10 states, day within its month), or
  the read returns 0: that is also how an absent chip shows. 12-hour images
  are converted. D10's leap selection is ignored on read.
- **Write.** STOP is held with /CS asserted across the reload of all 13
  registers in 24-hour mode, the divider is reset (register 13), and the
  registers are read back before STOP is released; a mismatch returns
  `BCERR_NOCLOCK`. The leap selection is written as 00 (Gregorian). The
  weekday is derived (1978-01-01 was a Sunday).
- **Exclusion.** Each call runs under `Forbid` and never waits; it takes
  about 2 ms (emulator). Call it from Tasks only. Port C is changed under
  `intoff`. The CIO rules are in
  [CIO U70 sharing](hardware.md#cio-u70-sharing).
- **Boot.** `kbattinit`, a resident module, adds the resource, enables Port B as a bit port
  if the ROM has not, makes PC1 an output with /CS high, and prints
  `battclock: YYYY-MM-DD HH:MM:SS` or `battclock: no clock`. timer.device,
  initialised next, starts the system time from it.

Departures from Amiga: an absent clock is seen by `ReadBattClock`
returning 0 (which is also 1978-01-01 00:00:00, as on Amiga); the
resource is there either way. `WriteBattClock` and
`ResetBattClock` return a status instead of nothing, so a set the chip did
not take is reported. The range ends in 2077, not 2114, because of the
chip's two-digit year.

## wd.device

The disk controller's resident driver (`<devices/wd.h>`), opened as
`OpenDevice(WDNAME, unit, ior, 0)` with a full `struct IOStdReq`. The
request model is upstream's
[ideplus.device](../../docs/reference/ideplus-device.md); the controller
protocol is COHERENT's `wd` driver.

- **Units.** `WD_UNIT(drive, part)` = `drive << 5 | part`. Drives 0–1 are
  the hard disks, 2–3 the floppies. Part 0 is the raw drive; part n (1–16)
  of a hard disk is kboot's `bi_part[n - 1]` (`WD_SLOT(drive, n - 1)`),
  applied to either hard disk as COHERENT does. An empty slot, a floppy
  part other than 0, or slots without a valid bootinfo block fail to open.
  Raw hard-disk units are read-only, as upstream's raw unit; write through a
  slot (kboot's convention puts one spanning the drive in slot 15). The raw
  hard-disk size is the highest slot end, because the controller has no
  capacity query (2^21 sectors without bootinfo). A floppy is 2392 sectors.
- **Transfers.** `CMD_READ` and `CMD_WRITE`: `io_Offset` is a zero-based
  512-byte sector within the unit, `io_Length` a nonzero multiple of 512
  (`IOERR_BADLENGTH`) inside the unit (`IOERR_BADADDRESS`). Success sets
  `io_Actual = io_Length`; failure sets 0, and a failed read's buffer is
  undefined. A buffer whose `kphys` is 512-byte aligned is DMAed directly,
  up to 128 sectors per command and never past the end of its physical
  64 KB page (a stack-segment buffer can lie across one; the request
  continues with a new command); any other (odd, unaligned, unmapped) goes
  one sector per command through `CMD_BUF`, copied by the Task (write) or
  the handler (read).
- **Other commands.** `WD_GETGEOMETRY` (12, upstream `GET_GEOMETRY`) fills
  `struct WDGeometry`, upstream's 12-byte record: 512, flags
  (`WDGF_READONLY`, `WDGF_DURABLE`, `WDGF_REMOVABLE`), sectors, and a
  generation, the drive's media change count plus 1. `WD_CHANGENUM` returns
  the change count in `io_Actual`. `WD_CHANGESTATE` issues Test Drive Ready
  and returns 0 (medium ready) or 1 in `io_Actual`. `CMD_UPDATE` and
  `CMD_CLEAR` complete at once: writes complete on the medium (terminal
  status) and nothing is cached. Others are `IOERR_NOCMD`.
- **Queue.** Both controllers share the doorbell, the completion vector and
  the bounce buffer, so one command is in flight and requests wait in one
  FIFO queue. Submission arms the controller's command block (status byte
  0xFF, opcode last), rings port 0x0500 with 1, and the `INTV_DISK` handler
  (`SetIntVector`) resets the PDMAC's IEO with 0, completes or continues the
  request, and issues the next. A completion while nothing is issued, or
  while the block is still armed, is counted in `WdStray` and dismissed;
  kboot's latched polled read is the first.
- **Errors** (positive `io_Error`, numbered as upstream's ideplus): status
  0x70 or 0x92 `WDERR_ABSENT`, 0x77 `WDERR_PROTECTED`, 0x73 `WDERR_MEDIA`,
  others `WDERR_DEVICE`. A floppy reporting 0x76 (media changed) has its
  change count incremented and the request re-run once from the start,
  since it was made for the medium now in the drive (COHERENT's raw
  policy); a second 0x76 fails `WDERR_CHANGED`.
- **Timeout.** A tick server counts each command down from 300 ticks plus
  10 per sector. On expiry the request fails `WDERR_TIMEOUT`, every queued
  request `WDERR_OFFLINE`, and the controller stays offline (later
  transfers fail `WDERR_OFFLINE`) until reset, because a late DMA could
  still land. This matches upstream's offline-until-cold-reset rule.
- **AbortIO** replies a queued request with `IOERR_ABORTED` and leaves its
  buffer alone; the issued request runs to completion, as upstream's issued
  transfers drain.

Departures, and why: upstream's `READ_SECTOR`/`WRITE_SECTOR` (one sector)
and `READ_SECTORS`/`WRITE_SECTORS` (1–4) are one pair, Amiga's
`CMD_READ`/`CMD_WRITE`, of any length, because direct DMA needs no staging
buffer; a filesystem adapter port maps them. Units encode drive and slot
(COHERENT's minor layout, widened for the raw unit) instead of upstream's
unit 0 plus APT slots 1–15. The change commands take numbers 16–17 because
Amiga's `TD_CHANGENUM`/`TD_CHANGESTATE` (13/14) collide with upstream's
`WRITE_SECTORS`/`READ_SECTORS`. There is no write-protect query, no format
command, no seek ordering of the queue (COHERENT sorts) and no
`DIAGNOSTICS` record; the drive parameters are kboot's (it issues Set Drive
Parameters from `kboot.cfg`), not set again. A re-run after a media change
lands on the new medium even for a mounted volume: a filesystem must
compare `WD_CHANGENUM` or the geometry generation itself.

## serial.device

The Z8030 SCC lines (`<devices/serial.h>`), opened as
`OpenDevice(SERIALNAME, line, ior, 0)` with a full `struct IOExtSer`.
Upstream has no RS-232 device (its SIO is the Atari bus), so the model is
classic Amiga serial.device; the request contract is upstream's
[device I/O](../../docs/reference/device-io.md).

- **Units** are kboot's `bi_serial` line numbers: 0 = 0x0100 (the ROM and
  kernel console; SCC U74 channel B in vector terms), 1 = 0x0120 (U74
  channel A), 2–5 = the LR board's U31 and U36 at 0x0300, 0x0320, 0x0380,
  0x03A0. Unit 1 is gameport.device's while its serial mouse is loaded
  ([gameport.device](#gameportdevice)), so it opens as busy. A line kboot
  did not prove present, any unit above 5, nonzero
  flags or a request shorter than `IOExtSer` fail to open
  (`IOERR_OPENFAIL`, `IOERR_BADLENGTH`). Without a valid bootinfo block only
  line 0 exists. Opens are exclusive unless every opener sets
  `SERF_SHARED` (`SerErr_DevBusy`). OpenDevice fills the request with the
  unit's parameters.
- **Commands.** `CMD_READ` waits for `io_Length` bytes, or with
  `SERF_EOFMODE` up to and including any `io_TermArray` byte; bytes already
  in the ring complete it quick. `CMD_WRITE` sends `io_Length` bytes, or up
  to the NUL when `io_Length` is −1, and completes once every byte is in
  the transmitter. The SCC holds one byte besides the one shifting, so at
  the line's real rate only an empty write or one byte to an idle line is
  sure to complete quick; the emulator sends at once. `SDCMD_BREAK` sends a break of the unit's `io_BrkTime`
  (default 250 ms, rounded up to ticks plus one) in order with the writes.
  `CMD_CLEAR` empties the ring and the latched status; `CMD_FLUSH` aborts
  every queued read, write and break; `CMD_RESET` does both and restores the
  defaults. `SDCMD_QUERY` sets `io_Actual` to the bytes buffered and
  `io_Status` to `SERST_*`. `SDCMD_SETPARAMS` takes `io_Baud`,
  `io_ReadLen`/`io_WriteLen` (5–8), `io_StopBits` (1–2), parity
  (`SERF_PARTY_ON`, `SERF_PARTY_ODD`), `io_RBufLen` (64–32768; a new size
  drops buffered bytes), `io_BrkTime`, `io_TermArray`, `SERF_EOFMODE`; it is
  refused while requests are queued (`SerErr_DevBusy`). `SDCMD_SETPARAMS`
  and `CMD_RESET` hold `Forbid`, so no other Task queues a request on the
  unit while it is checked, reprogrammed and its ring swapped; every other
  command changes shared state only under `Disable`. The time constant
  is PCLK/(32·baud) − 2 with PCLK 6 MHz, or 4 MHz when the ROM reports a
  4 MHz CPU; a rate outside the generator's range is
  `SerErr_BaudMismatch`. Defaults: 9600, 8N1, 512-byte ring.
- **Receive.** On first open the unit enables Rx-on-every-character,
  Ext/Status (break) and Tx interrupts; the last close disables them, drops
  DTR/RTS (except on line 0) and frees the ring. The Rx handler moves bytes
  into the ring and completes the reads it can. A full ring drops bytes and
  latches `SERST_OVERRUN`; the next completed read returns
  `SerErr_BufOverflow`. A special condition (SCC overrun, framing, parity)
  discards the byte and latches `SerErr_BufOverflow`, `SerErr_LineErr` or
  `SerErr_ParityErr` the same way. A received break latches
  `SERST_READBRK`.
- **Transmit** is one pump, called from BeginIO, the Tx interrupt and a
  tick server: it loads bytes while RR0 reports Tx Buffer Empty, at most 32
  per call so VI-off time stays bounded, completes finished writes and
  starts a queued break once All Sent is set. A Tx interrupt with nothing to
  send issues Reset Tx IP. The tick (`INTV_TICK` server, priority 90) ends
  due breaks and pumps every line with writes queued, so transmit
  progresses at up to 32 bytes per tick even if a Tx interrupt is lost.
- **Interrupts.** Each chip gets vector base 0x10 + 16·chip in WR2 with
  WR9 `MIE|VIS`: Tx, Ext/Status, Rx, Special at base+0/2/4/6 for the chip's
  lower-address channel and base+8…14 for the other. One handler per chip
  (`SetIntVector` on its 8 vectors) decodes the vector and ends with Reset
  IUS (WR0 0x38).
- **Boot** (`kserinit`). Every present line other than 0 is programmed
  9600 8N1, Rx and Tx enabled, DTR/RTS off, after a bounded All Sent wait,
  with interrupts off until opened.
- **Console line.** Line 0 keeps the ROM's rate and framing: boot only
  clears its interrupt enables, the reported rate is 9600 when the ROM's
  time constant matches, and SETPARAMS refuses any rate or framing change
  (it may still set the ring, break time and EOF mode). `KPutStr`/`KPrintf`
  are safe from any context. `kserinit` allocates the kernel's 256-byte
  transmit queue (`ktxq`, heap) and enables line 0's Tx interrupt, which
  stays on while the unit is closed. `con_putc` from a Task with VI on
  queues the byte and loads the transmitter if it is empty, waiting only
  while the queue is full; with VI off or at interrupt level it first
  sends the queue polled (`kcontx`), then its byte, so no byte passes a
  queued one and panic text is out before the halt. The pump sends the
  queue before unit 0's writes; the tick pumps it too, where a Tx
  interrupt does not come. `kcondrain` empties it before the end-of-run
  doorbell. A byte is loaded only after a Tx empty test with VI off, so
  it never overwrites one the pump loaded. Unit 0's output and the
  kernel's interleave by byte. Kernel cost: about 400 bytes of text, the
  queue from the heap; bss grows 6 bytes.
  While unit 0 is open its receive interrupt takes every input byte, so
  `con_getc` sees input only while unit 0 is closed.

Departures from Amiga, and why: no XON/XOFF (`SERF_XDISABLED` is always
reported, `io_CtlChar` is stored only) and no 7-wire handshake
(`SERF_7WIRE` is `SerErr_InvParam`), since the driver keeps line control
simple until a user needs it; no mark/space parity (`SEXTF_MSPON`), which
the SCC lacks. `io_Status` bits keep Amiga's positions but are active high.
Breaks are always queued (`SERF_QUEUEDBRK` ignored) and their length is the
unit's `io_BrkTime`. `CMD_STOP`/`CMD_START` are `IOERR_NOCMD`. A received
break does not end a pending read. Parameters persist across close and
reopen. CloseDevice with a request of the same reply port still queued
panics, as timer.device: as on Amiga, a caller aborts (`AbortIO`) and
collects (`WaitIO`) every outstanding request first. The tests' close
helper does so for a test that fails with a read still pending.

Untested: Tx interrupts, Ext/Status and Special Rx interrupts, modem
lines and break on the wire (the emulator raises only receive interrupts,
always reports Tx empty, All Sent, CTS and DCD, and ignores WR5), a 4 MHz
PCLK, and every path on hardware. LR-board lines are exercised only on the
emulator's model.

## parallel.device

The Centronics printer port (`<devices/parallel.h>`), opened as
`OpenDevice(PARALLELNAME, 0, ior, 0)` with a full `struct IOExtPar`. The
model is classic Amiga parallel.device; the request contract is upstream's
[device I/O](../../docs/reference/device-io.md). The lines are the ones
COHERENT's `lp` driver uses: data on CIO U70 port B, BUSY on U70 PC0,
/STROBE on CIO U66 PC3, /ACK on U66 PB1.

- **Unit 0** only, output only. Opens are exclusive unless every opener
  sets `PARF_SHARED` (`ParErr_DevBusy`); OpenDevice fills the request with
  the unit's parameters. Opening needs no printer.
- **Commands.** `CMD_WRITE` sends `io_Length` bytes, or up to the NUL when
  `io_Length` is −1, or with `PARF_EOFMODE` up to and including any
  `io_PTermArray` byte. It completes when the printer has taken every byte;
  `io_Actual` counts the bytes taken. An empty write completes quick.
  `CMD_FLUSH` aborts every queued write; `CMD_RESET` also restores the
  defaults; `CMD_CLEAR` does nothing (no input buffer). `PDCMD_QUERY` sets
  `io_Status`: `IOPTF_PARBUSY` while BUSY is high. `PDCMD_SETPARAMS` takes
  `PARF_EOFMODE`, `io_PTermArray` and `io_Timeout`, and is refused while
  writes are queued (`ParErr_DevBusy`). `CMD_READ`, `CMD_STOP` and
  `CMD_START` are `IOERR_NOCMD`.
- **Handshake.** Per byte: wait for BUSY low, put the byte on port B,
  pulse /STROBE, put port B back to 0, then wait for /ACK. /ACK is an
  edge pattern match on U66 port B (inverted PB1) at vector 0x70; its
  handler only clears IP/IUS and Signals. A byte with no /ACK counts as
  taken once BUSY is low at least two ticks after the strobe. Short bounded
  polls of /ACK and BUSY catch a fast printer without a Task switch; after
  16 bytes in one run the Task waits for the next tick, so it holds Forbid
  for at most 16 handshakes and leaves the CPU to other Tasks. Output
  is therefore at most 1.6 KB/s; on the Go model a byte to its 100 µs
  printer takes about 0.8 ms.
- **Timeout.** BUSY staying high before a byte, or no /ACK after one, for
  `io_Timeout` microseconds (default 10 s, 1 µs to 10 min, rounded up to
  ticks plus one) ends the write with `ParErr_LineErr`. A byte strobed but
  not acknowledged is not counted. An empty connector reads BUSY high, so a
  write with no printer ends after one timeout; nothing waits forever.
- **Driver Task.** Port B may be written only at Task level under Forbid
  ([CIO U70 sharing](hardware.md#cio-u70-sharing)), so the handshake runs
  in a Task (`parallel.device`, priority 5, 768-byte stack) created at the
  first open. It holds Forbid except while it Waits, so a battclock
  transaction never interleaves with a byte; battclock restores port B
  afterwards. The /ACK handler and a tick server (priority 70: BUSY low, a
  deadline due, or a paced burst) Signal it. After the last close it ends and its record
  and stack are freed. The request queue is Task-level state the driver
  Task shares with its clients, so BeginIO and AbortIO hold `Forbid`.
- **Port B.** The driver leaves port B as outputs holding 0, so PB7 (the
  clock's STOP) is low when idle. Bytes with bit 7 set pulse STOP for a few
  microseconds; the RTL gates STOP with /CS, the emulator does not.
- **Boot** (`kparinit`, before `kbeepinit` in `ResModules`). Resets CIO
  U66 and programs it for both its users: port B a bit port, PB1 inverted
  with a 0→1 edge pattern (OR mode) and vector 0x70, every port B line an
  input; port C PC3 output, high; ports B and C enabled; MIE without VIS.
  IEEE-488 on the same chip is unsupported.

Departures from Amiga, and why: `io_Timeout` is an Exec900 field appended
to `IOExtPar` so that a missing or jammed printer ends a write instead of
hanging it; Amiga waits until AbortIO. The port has no SELECT or PAPER OUT
input, so `IOPTF_PARSEL` and `IOPTF_PAPEROUT` are never set, and BUSY is
reported active high. `PARF_SLOWMODE`, `PARF_FASTMODE` and
`PARF_ACKMODE` are ignored: /ACK is always used. There are no reads. Writes
are not split or buffered by the driver; each byte is taken from the
caller's buffer. CloseDevice with a request of the same reply port still
queued panics, as timer.device.

Tests: `tests/parallel.c` on every target; it accepts either outcome of a
write (taken, or `ParErr_LineErr` within `io_Timeout`). Its `PRINTER:`
directive makes the Go model attach a printer and check what it printed.
The emulator models no U66 and no printer: BUSY reads back the U70 port C
latch and no /ACK comes, so there writes run at the two-tick fallback or
time out. Untested: the MiSTer core and hardware.

## beep.device

The internal speaker (`<devices/beep.h>`), opened as
`OpenDevice(BEEPNAME, 0, ior, 0)` with a `struct IOBeep`, plus the console
bell `con_bell()`.

```c
struct IOBeep {
	struct IORequest io_Req;
	ULONG io_Freq;		/* Hz, 3-20000; 0: a rest */
	ULONG io_Micros;	/* duration */
};
```

- **Why a new device.** Amiga has no beeper: its bell flashes the screen,
  and audio.device plays sampled waveforms on four DMA channels with
  allocation keys, priorities, periods and volumes. The C900 speaker can
  only sound one square wave at a time, on or off, so an audio.device
  subset would keep its names without their meaning. beep.device is the
  smallest interface that covers bells, alerts and simple tunes: one unit,
  frequency and duration, standard Exec request semantics.
- **Commands.** `CMD_WRITE` plays one tone; requests play one after
  another in submission order, from any number of shared opens, and each
  completes when its tone ends. `io_Micros` is rounded up to ticks, plus
  one tick when the tone starts between ticks, so a tone lasts at least
  `io_Micros` and at most 10 ms more. A zero duration completes at once;
  `io_Freq` 0 is a silent rest; any other frequency outside 3–20000 Hz is
  `BeepErr_Freq`. AbortIO ends a queued or sounding tone and starts the
  next. `CMD_FLUSH` aborts every tone; `CMD_RESET` also silences the bell.
  Other commands are `IOERR_NOCMD`.
- **Hardware.** CIO U66 C/T 2 in continuous square-wave mode with its
  external output on PB0, time constant 187500 / f (the counter clocks at
  PCLK/2, 375 kHz, and toggles once per period). PB0 low sinks the coil
  through 7407s, so the driver makes PB0 an output only while a tone
  sounds; silent, it is an input and floats high. MCCR and PBDD are
  read-modify-written with VI off; the mode register is written only with
  C/T 2 disabled. A tick server (priority 80) ends tones; nothing waits.
- **Bell.** `con_bell()` (`kernel/kernel.h`) sounds 440 Hz for 13 ticks,
  COHERENT's bell, unless a request is playing; a second bell restarts the
  length. It is safe from any context and does nothing before `kbeepinit`.

CloseDevice with a request of the same reply port still queued panics, as
timer.device. Tests: `tests/beep.c` reads C/T 2's registers back (it runs
wherever U66 answers as registers, the emulator included) and times
tones. The Go model can capture the speaker (`simrun -wav`); the pitch on
the wire has not been checked by a test. Untested: the MiSTer core and
hardware.

## keyboard.device

The C900 keyboard (`<devices/keyboard.h>`, events in
`<devices/inputevent.h>`), opened as `OpenDevice(KEYBOARDNAME, 0, ior, 0)`
with a full `struct IOStdReq`. Upstream's keyboard is the Atari's, behind
its input service (`abi/input.json`, not ported), so the model is classic
Amiga keyboard.device; the request contract is upstream's
[device I/O](../../docs/reference/device-io.md). The hardware is in
[the hardware reference](hardware.md#keyboard).

- **Raw codes** are Amiga raw key codes (`<devices/rawkeycodes.h>`, the
  AROS names), translated by one table from the keyboard's own 7-bit codes
  (PC XT set 1, [hardware](hardware.md#keyboard)); the C900 keys with no
  Amiga counterpart have the codes in
  [the platform contract](platform.md#keyboard). `ie_Code` is the code,
  with `IECODE_UP_PREFIX` (0x80) on release. Keyboard codes above 0x62 are
  dropped.
- **Qualifiers** (`ie_Qualifier`) are the state after the event: Shift
  `LSHIFT`/`RSHIFT`, Ctrl `CONTROL`, Alt `LALT`, Caps Lock `CAPSLOCK`, which
  toggles on each Caps Lock release (when the keyboard toggles its lamp),
  `NUMERICPAD` for keypad keys and `REPEAT` for a make of a key already
  down (the firmware's auto-repeat).
- **Commands.** `KBD_READEVENT` (9): `io_Length` holds whole
  `InputEvent`s (22 bytes; shorter is `IOERR_BADLENGTH`); it returns the
  buffered events that fit, linked through `ie_NextEvent`, `io_Actual` in
  bytes, and waits while none is buffered; a buffered event completes it
  quick. `ie_Class` is `IECLASS_RAWKEY`, `ie_TimeStamp` the tick count as
  seconds and microseconds. Pending reads are served in order. `KBD_READMATRIX`
  (10) copies up to 16 bytes of the down-key bitmap by raw code (bit
  `code & 7` of byte `code >> 3`). `CMD_CLEAR` empties both rings.
  Others are `IOERR_NOCMD`.
- **Buffers.** Every byte is taken with VI off by one routine, from the
  port A handler (`SetIntVector(INTV_KBD)`), from a tick server (priority
  80: bytes no interrupt took, and reads left pending) and from
  `kbd_getraw`. It acknowledges the byte (clear IP and IUS, then the PC3
  strobe), translates the code, updates the matrix and qualifiers, and
  appends the event to a 32-entry ring and each key press (code and the
  low qualifier byte) to a 32-entry ring for the console. A full ring
  drops the new entry and counts it in `KbdLost`; `KbdIntrs` and
  `KbdPolled` count bytes taken by interrupt and by polling. Keys are
  buffered whether or not the device is open.
- **Console input** (`kbd_getc` in `devices/conkeymap.c`): as Amiga
  console.device converts raw keys with the default keymap, the console
  takes the key presses (`kbd_getraw`) through a fixed US keymap:
  Shift selects the upper character, Caps Lock inverts
  Shift for letters, Ctrl gives the control code of a key in 0x40-0x7E or
  whose shifted character is in 0x40-0x5F (`^@` from 2, `^^` from 6,
  `^_` from `-`) and NUL for Space. Backspace is BS, Return and keypad
  Enter CR, Del 0x7F, keypad 00 two `0`s, the cursor keys `ESC [ A`/`B`/
  `C`/`D`. Function keys, Help, CE, W1-W3, Alt and the optional keys give
  nothing. Neither call replies a request, so neither switches Tasks.
- **Boot** (`kkbdinit`). Port A is disabled in MCCR, programmed as
  COHERENT does (bit port, all inputs, interrupt on the match PA7 = 1,
  vector 0x08) and enabled again; PC2 is made an input and PC3 an output;
  a byte already waiting is taken, else the interface is cleared; latch bit
  1 releases the keyboard. Then the handler and tick server are installed
  and port A's IE is set. `kbd_getraw` called earlier programs the port
  itself, with IE off.

Departures from Amiga, and why: eight C900 keys have no Amiga code and
take codes of keys the C900 lacks
([platform](platform.md#keyboard)); the console's keymap is fixed (no
keymap.library in Exec900, no `SETKEYMAP`) and its cursor keys send
`ESC [`, not CSI, the same sequence a serial terminal sends; there is one
Alt, no Amiga keys and no `RALT`/`LCOMMAND`/`RCOMMAND`. Auto-repeat is
the keyboard firmware's: a repeated make carries `IEQUALIFIER_REPEAT`,
which an input.device that times its own repeat discards. The
reset-handler commands
(`KBD_ADDRESETHANDLER`, `KBD_REMRESETHANDLER`, `KBD_RESETHANDLERDONE`) are
`IOERR_NOCMD`: the machine has no keyboard reset and Exec900 no reboot
path for them to precede. There is no input.device: a key reaches every
`KBD_READEVENT` reader in turn and also the console. Overflow is counted,
not reported as a 0xF9/0xFA event. CloseDevice with a read of the same
reply port still queued panics, as serial.device.

Tests: `tests/kbdnone.c` (every target; no key) and `tests/kbdkeys.c`
(Go model only, `KEYS:` injection of keyboard codes; the emulator's
`--key` input does not clear DAV on the PC3 strobe, so the driver reads
one byte repeatedly): raw codes, qualifiers including the keypad, the matrix and the
console's characters.
`tests/kbdmatrix.c` (emulator `EMUKEYS:`, Go model `KEYS:`) polls
`KBD_READMATRIX` from one binding while another Task holds a
`KBD_READEVENT` pending: Return, then Esc, held and released in turn,
each poll quick with nothing replied, and the holder gets the four
events.
Untested: the cable path and auto-repeat (the model's injection loads the
interface directly), the MiSTer core and hardware.

## Console

The operator's console: the display kboot names, a terminal emulator and
textcon.device (`<devices/textcon.h>`). Upstream's native console worker
(cell retention, windows, `CONCMD_` commands, ATASCII) is not ported; the
model is classic Amiga console.device on one fixed screen.

The console has two halves. The kernel keeps the serial line, keyboard
input, textcon.device and the card's probe and mapping, so it can always
print. Drawing (the terminal emulator, the card's backend and the font)
is a **display module** loaded from the boot volume, so kernel text holds
none of it and a machine without a card never loads it. The kernel also
lends the card's bitmap to another drawer through framebuf.resource
(below).

- **Card.** `con_init` (VI off, before the memory map) copies the ROM
  facts `mach_mem` needs, programs the SCC, then `kvidinit` probes the card
  a valid bootinfo block's `bi_console` names (LR, HR or LC; for
  `BI_CON_VID` or `BI_CON_ANY` HR, then LR) and maps it: LRSEG, or HRSEG0
  and HRSEG1. `mach_mem` keeps those descriptors. SER, no valid block or a
  failed probe: serial only, and no module loads. The probes are the
  backends' former ones: LR checks two patterns in the first two cells and
  the CRTC cursor registers; HR rejects a page kboot lists as RAM, checks
  the X port at 0x0404 and 0x0400 and two patterns in the bitmap, and
  derives the mode from the M/C strap in bits 1:0 of the 0x0404 read
  through `hrstrap` (1 colour, 2 mono, 3 mono 1024 lines, 0 no card) and,
  for mono, `BF_HR1024` for 1024 lines, unless the image's `khrcfg`
  word names the mode, which wins over both; LC checks
  the ID port, the zero status bits and R12. The LC console mode is not
  the kernel's: it is data, framebuf.resource's LC entry, which
  `mkdisk.py --lcmode` patches and `lc.display` reads, so choosing it
  adds no kernel text. Probing and mapping stay in
  the kernel because only kernel code may change the MMU (`kmapseg`) and
  the kernel must know the card to pick the module; they cost about 600
  bytes of text.
- **Module.** `kbootmods` loads `lr.display`, `hr.display` or
  `lc.display` from the boot volume for the card found, before the
  `Modules` list. Each file is linked from the common display half
  (`devices/condisp.c`), the emulator (`devices/vt.c`), one backend and
  what it needs: HR adds the font and `kglyphs`; LC adds both fonts.
  For HR the kernel then loads `gameport.dev`
  ([gameport.device](#gameportdevice)); other machines load it from
  `Modules` for the serial mouse. One module per card, chosen by the kernel, keeps the kernel's
  part to a name table and loads only the code the card needs; a single
  probing module would load every backend and need a mapping interface.
  Every boot disk carries all three (`mkdisk.py --file`). The module's
  romtag init opens textcon.device, reads the card with `CD_ASKDISPLAY`,
  sets up the backend for it (`vidsetup`), clears the screen, allocates
  its 256-byte draw queue, fills in `cd_Out`, `cd_Disp` and `cd_Reset`
  and attaches
  with `CD_SETDISPLAY`. It prints `console: lr` (or `hr`, `lc`). A missing
  or failed module leaves the console on serial: `exec900: module
  hr.display not loaded`.
- **Boot text.** Until the module attaches, every byte `con_putc` sends
  while a card is present is also kept in a 1 KB ring (`bootlog`, kernel
  bss). The first attach draws it, oldest first (after a wrap from the
  first whole line), so the screen shows the whole boot log; then the ring
  is no longer written. The boot log up to the module is about 200 bytes
  (banner, memory map, battery clock); 1 KB leaves room for five times
  that, such as module or device warnings, and covers more than a 25-line
  screen of typical lines. A crash before the module loads shows on
  serial only.
- **Lending** ([framebuf.resource](platform.md#framebuffer)). The
  resource is textcon.device's attach state: `kconinit` adds it for an HR
  or LC card, with one `FBMode` from a static table (HR in `CDM_` order,
  then LC). `AllocFB` under `Forbid` clears `conon` with VI off, records
  the owner and empties the ring; console bytes then go to the serial
  line and the ring, as before the module attached. Every Task draw
  tests `conon` under `Forbid` and holds it while it draws (`kvidputc`
  takes `Forbid` for a `CDO_DRAW` byte, `CMD_WRITE` holds it for the
  write), and interrupt level only queues, so no draw is in progress or
  starts once the caller has the bitmap. `FreeFB` calls the module's
  `cd_Reset` (the backend's `dp_Init`, a fresh emulator with the same
  reply and bell, the queue empty and the screen free), sets `conon`
  again and replays the ring. `kvidstop` does the same, VI off, when the
  bitmap is lent, before the message draws with `CDO_STOP`. Kernel cost:
  about 520 bytes of text and 210 bytes of data and bss.
- **Routing.** `con_putc` sends every byte to the serial line and then,
  once attached, to the module's `cd_Out`, so the test transcript is
  unchanged. A Task holding `Forbid` (as `KPrintf` and `KPutStr` do) with
  VI on does not draw each byte: `kvidputc` collects it in `bootlog`,
  unused while the module is attached, sets `SF_CONOUT` and draws the
  collected bytes in one `cd_Out` at a newline, when 1 KB is held, or at
  the outermost `Permit` (`xPermit` takes `kmaybe` when any `SysFlags` bit
  is set; `kvidflush`). A byte drawn or queued any other way, and every
  `CMD_WRITE`, draws the held bytes first, so the screen keeps the order
  of the serial line, except that interrupt-level bytes may pass a held
  partial line. A partial line printed in `Forbid` appears at the
  `Permit`; a Task that never permits shows it on serial only. A line
  costs one emulator call instead of one per byte. Kernel cost: about 250
  bytes of text, 2 of bss. `tests/conperf.c` times `KPrintf` and
  `CMD_WRITE` lines, with and without scrolling, on any card;
  `tests/kputq.c` checks queued and polled serial output keep their
  order and that a queued burst does not wait for a paced line;
  `tests/kputdead.c` that a dead-end alert sends the queue first.
  `CMD_WRITE` goes to `cd_Out` in 32-byte pieces, or to the
  serial line (`con_putc`, LF as CR LF) without a display. textcon.device
  tells `cd_Out` how it may draw: `CDO_STOP` after `kpanic`, a dead-end
  alert or a trap (`kvidstop`), `CDO_QUEUE` at interrupt level
  (`IntDepth`) or from a caller with VI off, else `CDO_DRAW`. `con_getc`
  reads the serial line, unless serial.device unit 0 is open (`kserbusy`;
  its receive interrupt owns the input), then (with a card) `kbd_getc`.
- **Drawing and interrupts.** One drawer holds the screen at a time
  (`conbusy`, in the module). `CDO_DRAW` draws under `Forbid` with the
  caller's interrupts on, so a scroll never holds VI off; `CMD_WRITE`
  holds `Forbid` for the whole write, so writes from different Tasks do
  not interleave. A `CDO_QUEUE` byte, or one that finds the screen held
  (a panic during a scroll), goes to the 256-byte queue the next drawer
  empties first; a full queue drops bytes. Such output appears on the
  screen late, after the drawing in progress, but on serial at once.
  `CDO_STOP` draws at once, polled, with VI off, unless the screen was
  held, so a panic after the module loaded is on the screen.
- **VI-off bound.** The console itself holds VI off only to take or give
  back the screen and to move one queued byte (tens of instructions).
  The LR backend and the HR backend (whose block moves are LDIR) draw with
  the caller's state; the LC backend holds VI off for one 8568 block
  piece of at most 255 bytes, about 1 ms, plus its ready waits. So the
  worst console VI-off section is about 1 ms on LC and well under 0.1 ms
  on LR and HR, below the 3 ms the SCC's receive FIFO covers at 9600.
  `tests/conlat.c` checks it: textcon.device scrolls 80 lines while
  124 bytes of serial input arrive through serial.device (no byte lost),
  and ticks are counted between two battery-clock second edges. On the
  Go model with CARD=hr it counts 700 ticks in 7 s (900 in 9 s before
  the 2-D scroll moves, 1400 in 14 s before text runs were drawn by
  `kglyphs`); with the old
  VI-off drawing it counted 576 in 13 s and fails. The Go model paces its
  serial input, so SCC overruns themselves are seen only on hardware.
  On the C emulator with `--hr` the check fails: its tick counts
  instruction steps, and an LDIR is one step however many words it
  moves, while its battery clock counts every word.
- **Backends** implement `struct Display` (`platform/display.h`): polled,
  VI-off-safe, no heap; `dp_Font` names the font a backend draws: 8x16
  on HR, 8x8 on LC, NULL on LR. The emulator hides the cursor (`dp_Cursor(r, c, 0)`)
  before it changes a cell or the position and shows it when the call
  ends, so a backend drawing the cursor in the cells is never overwritten.
  Erase and scroll fills pass `DA_NORMAL`. With autowrap on, the emulator
  passes each run of drawn characters within a row to `dp_Text` (LR, HR)
  in one call; a backend without it (LC) gets `dp_Put` per cell.
- **Interface** (`platform/display.h`, `<devices/textcon.h>`).
  `CD_ASKDISPLAY` stores the address of textcon.device's `struct
  ConDisplay` at `io_Data`: the card (`cd_Card`, `CDC_`) and HR mode
  (`cd_Mode`, `CDM_`), textcon.device's `cd_Reply` (emulator reports as
  input) and `cd_Bell` (`con_bell`), and the module's `cd_Out` and
  `cd_Disp`, and `cd_Reset` for the return from a loan. `CD_SETDISPLAY`
  with `io_Length` 1 attaches (while the bitmap is lent, it takes effect
  at `FreeFB`), 0 detaches (output then goes to serial only). These are Exec900 commands past
  Amiga's keymap commands; the display tests use them to find the backend
  and take the screen. Modules link their own copies of the port, mask and
  block-move helpers (`platform/port.s`, `block.s`); they reach exec only
  through `SysBase`.
- **Emulator** (`devices/vt.c`): deferred wrap at the right margin;
  scrolling at the bottom; LF also returns (Amiga's default line-feed
  mode, `CSI 20 h`/`l`). Controls: BEL (`vt_Bell`), BS, HT (every 8
  columns), LF, VT (cursor up), FF (clear and home), CR, CAN/SUB (cancel
  a sequence), ESC, and 0x9B as CSI. `ESC c` resets, `ESC 7`/`8` save and
  restore the cursor, `ESC D`/`E` index and next line. CSI: `A B C D`
  (motion, clamped), `E F` (next/previous line), `G` and `d` (column,
  row), `H`/`f` (position), `J K` (erase in display and line, 0/1/2),
  `M` (delete lines), `S` (scroll up), `m` (0, 1 bold, 4 underline, 5
  blink, 7 inverse, 22/24/25/27 off; colours ignored), `6 n` (reports
  `CSI row;col R`), `s`/`u`, `?7 h`/`l` (autowrap), `?25 h`/`l` and
  Amiga's `CSI 0 SP p` / `CSI SP p` (cursor), `CSI 0 SP q` (reports
  `CSI 1;1;rows;cols SP r`). Up to 4 parameters; others are ignored.
  Characters 0x20-0x7E and 0xA0-0xFF are drawn; DEL and other C1 codes
  are ignored.
- **textcon.device.** `OpenDevice(TEXTCONNAME, CONU_STANDARD, ior, 0)`
  with a full `IOStdReq`; any number of opens share the one screen.
  `CMD_WRITE` sends `io_Length` bytes, or up to the NUL when it is -1, as
  above, and completes at once. `CMD_READ` returns as soon as at least
  one character is buffered, up to `io_Length`. While the device is open
  a tick server (priority 80) moves up to 8 characters per tick from
  `con_getc` into a 64-byte ring (the oldest is dropped when full) and
  completes reads in order; the emulator's reports enter the same ring.
  `CMD_CLEAR` empties it; AbortIO replies a queued read with
  `IOERR_ABORTED`; `CD_ASKDISPLAY` and `CD_SETDISPLAY` are above; other
  commands are `IOERR_NOCMD`. CloseDevice with a read of the same reply
  port still queued panics, as serial.device does.

Departures from Amiga, and why: there are no windows, so `io_Data` at open
is ignored, unit 0 is the only unit (no `CONU_LIBRARY`, `CONU_SNIPMAP`,
character-mapped units or `RawKeyConvert`), flags are accepted and
ignored, and resizing, `CSI t u x y` and the keymap commands
(`CD_ASKKEYMAP` ...) are unsupported. Insert/delete characters, insert
lines and scroll down (`@ P L T`, `ESC M`) are unsupported, since the
backend contract has no scroll-down or cell read-back. Raw input events
(`CSI 1 {`) are not reported; the characters the console keymap gives
(`kbd_getc`) are what a read returns. Input is polled at
the tick, so a character can wait up to 10 ms. Serial input still
reaches the console while serial.device unit 0 is closed. The kernel's
output and CMD_WRITE share the emulator state (attributes, a sequence in
progress), so a sequence split across a kernel message is applied to it.
Amiga's console.device is one ROM module drawing through graphics.library;
here its drawing half is a separate disk module per card, attached through
the two Exec900 commands, because kernel text is one 64 KB segment.

`tests/vt.c` drives the emulator on a fake 10x4 display on every target;
`tests/condev.c` runs textcon.device on the serial console with scripted
input; `tests/lrcon.c` reads the LR card back on the Go model (and checks
no display is claimed elsewhere); `tests/bootlog.c` reads the LR or HR
screen back and finds the banner and memory-map lines printed before the
module loaded above its `console:` line. framebuf.resource:
`tests/fbres.c` (every target) checks the resource is absent without a
bitmap card and, with one, the description and the ownership refusals;
`tests/fbshare.c` (Go model, `CARD=hr`) draws a pattern into both halves
row by row with `FB_ADDR` and `FB_ROWSPLIT` and reads it back, checks the
bitmap's word sum is unchanged while the console prints (`KPrintf`,
`CMD_WRITE`, a recoverable alert) and gameport.device opens, then that
`FreeFB` clears the bitmap and the text printed while lent is on the
screen; `tests/fbpanic.c` ends in a dead-end alert while lent (its screen
capture shows the replayed text and the alert).

## LR display

`devices/lr.c` is the `struct Display` backend of `lr.display`, 80x25
cells ([hardware reference](hardware.md#devices)). textcon.device's probe
maps LRSEG over physical 0x37 ([Console](#console)).

- **Init.** Video off (0x0418 = 0), every cell blank (0x0720), R0-R15
  from the boot ROM's table, video and blink on (0x28).
- **Cells** are word writes (attribute high, character low): `DA_INVERSE`
  0x70, `DA_UNDERLINE` 0x01, else 0x07, plus 0x08 for bold (no visible
  effect on the mono LR video) and 0x80 for blink. Scroll copies the rows
  with `kwmove` and fills with `kwfill` (LDIR); the start-address
  registers stay 0.
- **Cursor.** The CRTC's: R14/R15 = row * 80 + column, R10 0x0B (steady,
  lines 11-12) or 0x20 (off).

## LC display

`devices/lc.c` is the `struct Display` backend of `lc.display` for the
LC card's 8568, 80x25 text in 8x8 cells at 640x200 or, when
framebuf.resource's LC mode is 640x400 (`mkdisk.py --lcmode=400`,
[framebuffer](platform.md#framebuffer)), in 8x16 cells interlaced.
`vidsetup` reads the mode with `GetFBInfo` once; init and `cd_Reset`
then use the same registers and font. textcon.device's probe writes
0x3E to the ID port 0x01FF, so an empty bus cannot echo, and reads 0xC1;
status bits 0x18 read 0; register R12 holds 0x55 and 0xAA and is left at
0.

- **Init.** Every register is written (the chip resets R0-R36 to 0), R28
  first so every later address is a 64 KB one: COHERENT's 80x25 timing,
  text at VRAM 0x0000, attributes at 0x1000, font at 0x2000 (256 glyphs of
  16 bytes). R36 = 5 refresh cycles a line; R37 = 0xC0, positive syncs, as after
  reset ([hardware reference](hardware.md#quirks)). The
  font area is cleared by block fill and glyphs 0x20-0x7E are written,
  8 rasters into each 16-byte slot, from the 8x8 font: R9 = 7 (8 lines
  per row, so the chip reads 16-byte glyphs) and R23 = 8 show all eight.
  Other codes are blank. At 640x400 five registers change (`lcil`: R8 =
  3, R9 = 0x0F, R11 = 0x0F, R23 = 0x10, R29 = 0x0F; the cursor is R10 =
  0x0E to R11, lines 14-15 or 14 alone) and the 8x16 font fills each slot
  ([hardware reference](hardware.md#lc-interlace)); the VRAM layout is
  the same.
- **Font.** `devices/font8x8.c` (`font8x8[]`, 8 bytes per glyph from
  code 0x20, 760 bytes; `dp_Font`), generated by `tools/mkfont8x8.py`
  from Daniel Hepper's `font8x8_basic.h`
  (<https://github.com/dhepper/font8x8>, public domain; after Marcel
  Sondaar's `font8x8.h`, which credits the IBM VGA fonts and is likewise
  public domain). The input used had SHA-256
  `49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5`.
  At 640x400, `devices/font8x16.c` ([HR display](#hr-display)), linked
  into `lc.display` as well, since one display module loads per boot and
  the kernel has no room for a shared copy: `lc.display` carries both
  fonts (2,280 bytes).
- **Access.** Every data-register write and block start waits for Update
  Ready (status bit 7, at most 30000 polls). The driver keeps the register
  index, the update address and R24 as last written and skips repeats. A
  fill is one data write then R30 counts; a copy sets R24 bit 7, the source
  R32/R33 and the destination R18/R19. Blocks are at most 255 bytes, since
  R30 = 0 is not used. Scroll copies only ascending (destination below
  source), which is all the chip does.
- **Attributes.** `DA_*` map to white (RGB), plus intensity for bold,
  reverse, underline and blink. Each row's attribute is cached when the
  whole row has one, so a character in such a row is a single data write,
  and a scroll of uniform rows copies only the text plane.
- **INTR.** The 8568 raises INTR on every Update Ready rise: each data
  write, block, and update-address write. Each entry runs with VI and NVI
  off (`intoff`) and ends with a status read that finds the chip ready,
  which releases INTR, so no NVI follows. A long block reopens interrupts
  between 255-byte pieces (about 1 ms each), keeping SCC receive and the
  tick serviced ([VI-off budget](#vi-off-budget)). A chip that never becomes ready disables the backend; the
  NVI path then releases whatever it left.
- **Reset.** Init is also `cd_Reset`, which `FreeFB` and a crash run
  after a bitmap owner may have reprogrammed the chip
  ([framebuffer](platform.md#framebuffer)): it rewrites every writable
  register, forgets the cached index, address and R24, reloads the font
  and clears both planes, so no owner state survives. VRAM outside
  0x0000-0x07CF, 0x1000-0x17CF and 0x2000-0x2FFF is left as it is; text
  mode does not show it. The font load lets interrupts in after each
  glyph; as one section, 760 data writes and 95 seeks (1,520 writes at
  640x400), it held VI off for an estimated 6-12 ms.
- **Limits.** Callers serialize entries; the console never enters the
  backend from interrupt level or while another entry is in progress.
  The cursor (lines 6-7, steady), R37 and the colours have not been seen on
  a monitor.

`tests/lcdisp.c` reads the chip's RAM and registers back after init,
cells, a scroll, an erase across a row boundary, a cursor move and the
mode (R8, R9, R11, R23, R29, and the glyph rows) framebuf.resource
describes, and checks no NVI was counted; `tests/lcdisp400.c` runs it on
a `--lcmode=400` disk. Both fit the emulator's LC card (`LC: card`). Unused register
bits read 1, so readbacks are masked. `tests/host/test_lcfont.py` checks
the font table's dimensions against R9, R23 and the glyph stride. Without the card it checks no NVI was
counted since boot.

## HR display

`devices/hr.c` is the `struct Display` backend of `hr.display`, drawing
8x16 cells into the bitmap ([hardware reference](hardware.md#hr-card)).

- **Probe** (textcon.device's, [Console](#console)). A valid bootinfo run
  covering physical page 0x3E means RAM, not a card. Otherwise the ID
  word at 0x0404 must have bits 1:0 nonzero and bits 12:10 zero, as must
  the X port, and the bitmap's first two words must hold two
  complementary patterns (then restored) through HRSEG0. Only then is
  HRSEG1 mapped; a failed probe leaves both inhibited.
- **Geometry** (`vidsetup`, from `cd_Mode`). Strap 1: 640x400 RGBI,
  80x25 cells. Strap 2: mono, 128x64 cells when kboot sets `BF_HR1024`,
  else 128x50; strap 3 (the documentation's 1024x1024 code, which the core
  never reads) also gives 128x64. A `--hrmode` setting replaces the
  strap read ([platform](platform.md#framebuffer)).
- **Mapping.** Bitmap offsets are linear (0-0x1FFFF): HRSEG0 maps the
  first 64 KB, HRSEG1 the second, both system read/write. Block moves and
  fills are split at the 64 KB boundary on both sides.
- **Font.** `devices/font8x16.c` (`font8x16[]`, 16 bytes per glyph from
  code 0x20; `dp_Font`), HR's 8x16 font, generated by
  `tools/mkfont8x16.py` from console-setup's `Lat15-VGA16.psf` (glyphs
  from `u_vga16.bdf`, MIT licence, Dmitry Bolkhovityanov; the notice is in
  the file): ASCII 0x20-0x7E only, 1,520 bytes. Other codes draw as `?`.
  The ROM's and COHERENT's HR fonts are not used: neither is offered under
  a licence.
- **Drawing.** Mono cells with no attribute go through `kglyphs`
  (`platform/glyphs.s`), which draws a run of glyphs with an unrolled
  16-raster copy; `dp_Text` computes the run's bitmap address once.
  Colour runs go through `kglyph4`, which maps each glyph byte to eight
  pixels through a 256-long table built for the attributes (1 KB, from
  the heap in colour mode); underline then fills the last raster. Other
  mono attributes, and colour text row 12, which spans the two bitmap
  segments, draw a cell at a time in C.
- **Attributes.** Mono: inverse inverts the cell, bold ORs the glyph with
  itself shifted one pixel right, underline sets the bottom raster. Colour:
  foreground 7 (white), bold 15, background 0, inverse swaps them. Blink is
  ignored.
- **Scroll.** The card has no display start register, so a scroll copies
  the bitmap with LDIR (interruptible). For each text row the driver
  keeps how many leading cells may be non-blank (`hrlen`): a row receives
  the longer of its own and the next row's length, which also blanks what
  it held, and consecutive rows needing the same length move as one
  `kmove2d` (an LDIR per raster) while both sides stay in one segment.
  The bottom row is blanked over its old length with `kfill2d`.
  On the Go model at 1024x800 a full-screen scroll takes 162 ms with
  full-width lines and 52 ms with quarter-width ones; the copy is bound
  by the bitmap's memory cycles (about 20 clocks a word under LDIR).
- **Cursor.** A block: the cell is XOR-inverted (colour: XOR 0x7777,
  which swaps white and black, so text under it reads inverse). Put or erase over the
  shown cursor redraws and re-inverts it; a scroll covering its row hides
  it before the copy and shows it again after. `dp_Cursor` with `on` false
  hides it.
- **Init** writes control `FBHR_FULL` (0x0F: banks, mono enable, full
  intensity, `<resources/framebuf.h>`) and blanks all 128 KB. It also
  runs when framebuf.resource returns the card.
- **Limits.** Callers serialize entries. textcon.device probes in
  `con_init`, before `mach_mem()`, which keeps the HRSEG0/HRSEG1
  descriptors the probe programmed and rebuilds the rest of kmmutab. Only 1024x800 has run (Go model);
  1024x1024 and colour are untested, and no mode has been seen on a
  monitor.

`tools/hrtext.py SCREEN.pbm` reads a mono screen capture as text in this
font (simrun's own `.screen.txt` reads the ROM's font).
`tests/hrdisplay.c` checks cells, attributes, the `?` substitute, both
bitmap halves, erase and the cursor in the bitmap; `tests/hrscroll.c`
full and region scrolls with the cursor, times four full-screen scrolls
of full and quarter-width lines and leaves a banner. Both find the
backend with `CD_ASKDISPLAY` and detach it from the console with
`CD_SETDISPLAY` first; without a card they only pass.

## gameport.device

A mouse (`<devices/gameport.h>`, events in
`<devices/inputevent.h>`): the HR card's J2 mouse, or a Mouse Systems
serial mouse on SCC U74 channel A without an HR card. Opened as `OpenDevice(GAMEPORTNAME, 0, ior,
0)` with a full `struct IOStdReq`. Upstream has no game port (its pointer
input is `abi/input.json`, not ported), so the model is classic Amiga
gameport.device.

- **Sampling.** While the controller type is `GPCT_MOUSE` each sample
  adds its motion to the pending motion and compares its buttons with
  the last state. HR: a server on the 100 Hz tick (priority 0) reads
  both ports and takes the 10-bit differences (sign-extended, so a wrap
  is invisible below 512 steps a tick): a sample a tick. Serial: a
  sample a half packet (below).
- **Serial backend.** The mouse sends 5-byte packets at 1200 8N1:
  `0x80 | {L,M,R}` (bit 2 left, 1 middle, 0 right; 0 = pressed), then
  dX1, dY1, dX2, dY2, signed bytes, X right and Y up. It sends only on
  motion or a button change, back to back while moving (41.7 ms a
  packet), and reads no handshake line. A reader Task (`gameport.device`,
  priority 10, 768-byte stack) sets the line to 1200 8N1 with a 64-byte
  ring, hunts a header (`(byte & 0xF8) == 0x80`) one byte at a time,
  then reads the two halves with two-byte `CMD_READ`s. Each half is one
  sample: the header's buttons, dX and −dY, so motion is right and down
  positive as on HR. A line error or short read drops the rest of the
  packet and hunts again. With the type `GPCT_MOUSE` the reader queues
  the events under `Disable` and replies the pending reads; otherwise it
  keeps only the buttons. Every button change is its own sample, so a
  click shorter than a tick is not lost. **Granularity:** HR gives an
  event a tick at most (100 Hz); the serial mouse an event a half
  packet (about 48 Hz while moving, halves 25 and 16.7 ms apart), the
  finest its data has. Without a mouse, or with the OSD option off, no
  bytes come and there are no events; nothing detects the mouse.
- **Events.** A button change allowed by `gpt_Keys` (`GPTF_DOWNKEYS`,
  `GPTF_UPKEYS`) gives `IECODE_LBUTTON`, `IECODE_MBUTTON` or
  `IECODE_RBUTTON`, with `IECODE_UP_PREFIX` on release. Pending motion of
  at least `gpt_XDelta` or `gpt_YDelta` steps (0 counts as 1) gives
  `IECODE_NOBUTTON`. Every event carries the motion pending until then
  (`ie_X`, `ie_Y`, right and down positive) and
  `IEQUALIFIER_RELATIVEMOUSE` plus the buttons held after it
  (`LEFTBUTTON`, `MIDBUTTON`, `RBUTTON`). With `gpt_Timeout` nonzero, that
  many ticks without an event give an `IECLASS_TIMER` event. The time stamp
  is the tick count (`ex_TickCount`) as seconds and microseconds. Eight events are queued; a
  full queue adds further motion to its newest event and loses button
  changes.
- **Commands.** `GPD_READEVENT` (9): `io_Length` holds whole `InputEvent`s
  (22 bytes; shorter is `IOERR_BADLENGTH`); it returns the queued events
  that fit, linked through `ie_NextEvent`, `io_Actual` in bytes, and waits
  while none is queued; a queued event completes it quick. Pending reads
  are served in order. `GPD_ASKCTYPE`/`GPD_SETCTYPE` (10, 11) read and set
  the type byte at `io_Data`: `GPCT_MOUSE` adopts the current counts and
  buttons (serial: the buttons last decoded) as the reference and
  empties the queue; `GPCT_NOCONTROLLER` and
  `GPCT_ALLOCATED` stop sampling; joystick types are `GPDERR_SETCTYPE`.
  `GPD_ASKTRIGGER`/`GPD_SETTRIGGER` (12, 13) copy a `GamePortTrigger`.
  `CMD_CLEAR` empties the queue, `CMD_FLUSH` aborts every pending read.
  Others are `IOERR_NOCMD`.
- **GetGPButtons** (LVO −42, [platform](platform.md#mouse)) reads
  `inw(0x0400)` bits 15:13 (HR) or `gp_SButtons`, the reader's last
  header (serial; one word, written under `Disable`), and maps them with
  the events' qualifier code. It takes no lock and touches no device
  state, so a poller needs no binding or controller type and cannot
  disturb a reader.
- **Boot.** gameport.device is its own boot module, `gameport.dev`
  (`devices/gameport.c` with the port helpers: 4,332 bytes of code, 358
  data, 126 bss). `kbootmods` loads it after `hr.display` from the same
  per-card name table (`kvidmod(1)`); on any other machine it loads only
  when `Modules` lists it. Its init picks the backend: HR when
  framebuf.resource exists and `fbi_Card == FBC_HR`, otherwise serial
  (LR, LC, no card, `bi_console` SER). An HR card beside another console
  card is therefore not used. A second load (HR with `gameport.dev` also
  listed) finds the device in `DeviceList` and does nothing. The HR
  backend does not depend on `hr.display` or on who owns the bitmap; it
  never writes the card's ports. The initial trigger is both key
  directions, no timeout and deltas of 1.
- **Serial line ownership.** The serial backend opens `serial.device`
  unit 1 (0x0120, U74 channel A) exclusively at load, before the device
  exists, and never closes it: any other `OpenDevice` of unit 1, shared
  or not, fails with `SerErr_DevBusy`, serial.device's normal answer for
  a busy unit. If unit 1 does not open (no line, or already open) the
  device is not created (`gameport.device: serial line 1 not
  available`). Unit 0, the console (channel B), is untouched. Software
  cannot read the core's OSD option, so listing `gameport.dev` in
  `Modules` is the switch: list it when the mouse is enabled; leave it
  out to keep unit 1 for serial.device users. Listed while the option
  is off, it holds the line and gives no events.

Departures from Amiga, and why: one unit (one mouse), mouse
only (no joystick hardware). `GetGPButtons` is an Exec900 base function: Amiga
intuition read CIA-A and POTGO itself to see the buttons while an alert
was up, but here the ports and serial unit 1 are the driver's, and an
alert shown on input.device's Task cannot wait for its own events. The serial mouse is gameport.device's, not
an input.device handler reading serial.device as on the Amiga, so both
mice look the same to clients and there is no input.device yet. `gpt_Timeout` counts 100 Hz ticks and the
deltas counter steps. `GPD_READEVENT` with no controller set is
`GPDERR_SETCTYPE` instead of waiting forever. The last CloseDevice resets
the type to `GPCT_NOCONTROLLER` and empties the queue, so sampling stops
with no user. There is no input.device to merge mouse and keyboard
events. CloseDevice with a read of the same reply port still queued
panics, as timer.device.

`tests/gameport.c` (Go model with `CARD=hr`, `MOUSE:` injection) checks
the type and trigger commands, a timeout event, abort and flush, motion
sums and both buttons' down and up events with their qualifiers, and the
type reset by the last close. Without a card it checks the device is
absent. `tests/sermouse.c` (emulator, `gameport.dev` in `Modules`, a
`SEND:` wire peer on line 1) checks that unit 1 is busy for serial.device
and the decoded events in order: two junk bytes skipped, one event per
half packet with motion, Y negated, left, right and middle down and up
with their qualifiers, and no event after the last packet. Then, with
another Task holding the unit and a read pending, it polls
`GetGPButtons` through three more packets sent one at a time (`SEND:`
stages): left, right and middle, none. `tests/gpbuttons.c` (Go model
with `CARD=hr`, `MOUSE:`) polls `GetGPButtons` the same way, from a Task
that never opens the device, through left, then right and middle, each
pressed and released. The emulator
delivers wire bytes unpaced, so 1200-baud timing is untested. Untested:
HR's middle button, queue overflow, the serial mouse on the Go model,
the MiSTer core and hardware.

## lance.device

`devices/lance.c`, a boot module ([network](platform.md#network)).
`lninit` probes the card (RAP reads back 3, then a stopped CSR0 reads
exactly STOP, which an echoing bus cannot give), reads the PROM, builds
the base with `MakeLibrary`, adds a server on vector 0x88 and adds the
device.

- **DMA block.** The first open allocates one block: the 24-byte init
  block, then at offset 32 the 8 receive descriptors, the 2 transmit
  descriptors and the 10 buffers of 1,520 bytes. It is under 64 KB, so
  it lies in one physical page, and `CachePreDMA` gives its physical
  base; every chip address is that base plus an offset. CSR3 is 0x0007.
- **Starting.** `lnstart` runs with VI off: STOP, empty both rings if
  the chip was running, rebuild the init block (mode, station address,
  LADRF from the multicast table, ring addresses and lengths), give
  every receive buffer back, write CSR1-3, set INIT and poll IDON (2,000
  reads at most), then IDON|STRT|INEA. Transmit descriptors the chip
  still owns move to the ring's start in order, with their requests and
  buffers, because INIT restarts the chip's ring pointers there: a filter
  change or an underflow restart loses no queued write. It runs at
  `S2_CONFIGINTERFACE` and `S2_ONLINE`, on every multicast or
  promiscuous change while online, and from the server after an
  underflow, BUFF, MERR or BABL.
- **Receive.** The server empties the ring in order: a good frame
  (STP|ENP, no ERR) goes to `lndeliver` with MCNT less the FCS, then its
  descriptor is given back at once. `lndeliver` drops a multicast frame
  whose group the table lacks (a hash collision; counted as
  `S2SS_ETHERNET_BADMULTICAST`), gives the frame to the first matching
  read of each opener whose filter accepts it, and otherwise to the
  first orphan read.
- **Transmit.** BeginIO queues a write and, with VI still off, moves
  queued writes into free buffers (`lntxfill`): header from
  `ios2_DstAddr`, the station address and the type, payload through
  CopyFromBuff, padding to 60 bytes on unit 0, then the descriptor
  (OWN|STP|ENP) and TDMD. The server replies each write whose descriptor
  the chip has released, counts retries and underflows from TMD1 and
  TMD3, and refills from the queue. A write in a buffer cannot be
  aborted; AbortIO of one returns −1 and it completes normally.
- **Exclusion.** Every queue, the rings, the statistics and the chip
  registers are changed with VI off; the server runs with VI off. RAP is
  0 at every other time, so the server reads CSR0 without selecting it.
  Open, Close and Expunge run under OpenDevice's Forbid; BeginIO and
  AbortIO need no Forbid.
- **Copies with VI off.** Both copies run with interrupts off, so a
  full frame holds off every other interrupt for its copy (estimated at
  1-1.5 ms at 6 MHz with CopyMem's word move; not measured). That is
  about the time the SCC's 3-byte receive FIFO covers at 19,200 baud;
  faster serial input can overrun while full-size frames arrive.
- **Interrupt stack.** `tests/lanloop` paints the interrupt stack and
  measures the server's depth after a burst: 282 of its 512 bytes on
  the Go model, through the opener's CopyToBuff (CopyMem), the deepest
  dispatch measured so far.

## Hunk loading

The contract is [Program loading](platform.md#program-loading) and
[Boot modules](platform.md#boot-modules).

- **Conversion.** `ld -r` keeps relocations and resolves `SS` and
  `SysBase_` against `progabs.o`, leaving them as absolute (`L_ABS`)
  records mkhunk skips. Its output lays text, data and bss out
  contiguously from linear 0x030000; a relocation record gives the linear
  address of its field and the l.out segment of its target, so mkhunk
  subtracts that segment's base from the field and writes the target
  hunk's offset. Text becomes the code hunk, shared and private data the
  data hunk, `BSSD` the bss hunk. Data pointers come out of the linker
  with bit 31 clear and instruction operands with it set; both are kept.
  `LR_SHORT` and `LR_CALL` records are already `LR_LONG` after `ld -r`.
- **Several inputs.** `ld -r` cannot make text over 64 KB (only `-L`
  pads modules to segment boundaries, and a segment it fills leaves no
  room for the loader's header), so `--split` links groups
  separately. Each input's sections get a hunk and offset; a
  symbol-based record is resolved against another input's global. Its
  field holds the addend: `0x80` (or `0x7F` when negative) in the top
  byte and the offset for an instruction operand, the sign-extended
  addend for a C pointer. A single input converts byte for byte as
  before. The loader needed no change: every hunk was already its own
  block, and relocations already named any target hunk.
- **Reader.** `InternalLoadSeg` reads header and type longs through
  `funcarray[0]` one at a time, each code or data hunk in one read, and
  relocation offsets up to 128 longs per read into a 512-byte buffer
  allocated with the hunk table for the load. A read per offset cost
  about 5,000 clocks a relocation. The boot reader keeps a data block and an indirect block
  cached, so those small reads cost no disk access. It reads through
  `wd.device` with `DoIO` from the root Task, so it needs interrupts and
  runs after `tick_init`.
- **Cost.** Loader and boot modules add 2,314 bytes of kernel text and
  94 of data. The kernel's text is 48,746 bytes whatever the root
  program, since the display stack became modules
  ([Console](#console)) and DOS is modules too ([DOS](#dos)); programs
  and modules take heap memory instead.
- **Root program.** `kbootmods` loads the display module and the modules,
  then the program
  `Startup` names, and returns its seglist; `cmain` enters it through
  `krun(entry)`, a function of its own so that a debugger breakpoint on
  `krun` finds the entry (`tools/perf.py`). The seglist stays loaded:
  the system stops when the program returns, and Tasks it started may
  still run its code until then.

## DOS

The contract is [DOS](platform.md#dos). dos.library follows AmigaDOS V36
(classic Amiga is the default model); upstream's `lib/dos` and `lib/fs`
were read for structure (packets, handlers as workers, the cooked line
editor), but their policy departs from the Amiga's and was not ported.

- **Library.** `dos/doslib.c` is the romtag and base: `RTF_AUTOINIT`,
  vectors from `DosFuncs`, which `tools/genlvo.py --table` generates from
  `fd/dos_lib.fd` with `dlunimpl` for every vector the fd leaves
  unimplemented. Each function is `xName(DOSBase, args...)`; inside the
  library the files call each other's `x` functions directly. Init
  builds the `RootNode` and `DosInfo` (static, in the module's data) and
  the six late assigns. Expunge never frees it.
- **ramlib** (`dos/ramlib.c`, [contract](platform.md#disk-libraries-and-devices)).
  Init patches exec's open, close and remove vectors and adds a
  low-memory handler. A load runs under one `SignalSemaphore` (`rlsem`)
  and checks the list again once it holds it. Each loaded seglist gets
  a 14-byte record (16 allocated: next, base, seglist, device flag); a close or
  remove whose result is a recorded seglist unloads it. The ramlib
  process's port is static and starts `PA_IGNORE`, so requests sent
  before the process has its signal wait in the queue. The plain-Task
  path costs a `CreateMsgPort` per load. `ramlib.c` adds 3,600 bytes
  to the dos.library hunk file.
- **Alert display** (`dos/ramlib.c`, [contract](platform.md#alerts)).
  Init also patches `Alert`: exec's, then intuition's `DisplayAlert` for
  a recoverable alert from a Task outside `Forbid` and `Disable` on its
  own stack. An interrupt is told apart by its stack: servers and
  handlers run on the interrupt stack, outside every Task's
  `tc_SPLower`..`tc_SPUpper`, and ExecBase has no interrupt depth to
  read. The text (about 110 bytes) is built on the caller's stack.
  Alerts take turns on a `SignalSemaphore` (`rlalsem`); a Task waits
  only after the display conditions hold and it has opened intuition,
  never if it is the semaphore's owner, the ramlib process or
  `input.device`'s Task. The patch adds 616 bytes of code, 162 of data
  and 50 of BSS to dos.library, and no kernel text or stack-segment
  reservation. The kernel's `AN_BadLVO`
  reaches the patch: `kbadlvo` calls `kalertv`, the label `genlvo.py`
  puts on the `Alert` vector, with a direct call the size of the one to
  `xAlert` it replaces, so kernel text stays 53,244 bytes.
  `tests/alertint.c` checks it with a fake intuition.library that
  records its calls and can hold one on the display.
- **Crash requester** (`dos/doscrash.c`, `dheld` in `dos/dosasm.s`,
  [contract](platform.md#task-crashes)). Init saves
  `SysBase->TaskTrapCode` and installs `dtrap`. `dtrap` runs in trap
  state (VI and NVI off, on the trapping stack) and may not `Wait`, so
  to hold it rewrites the frame and returns: r0-r1 the trap number,
  r2-r3 the PC, r4 the stack offset to run on, r5 the trapping FCW;
  FCW system mode with VI, NVI and the trapping EPA bit; PC `dheld`. The
  kernel's `IRET` then resumes the Task at `dheld`, which loads r15 and
  calls `dcrash`. `dtrap` also rewrites the guard word, so the switch's
  stack probe stays quiet. The crash process's port is static and starts
  `PA_IGNORE`, as ramlib's, so a crash queued before the process has its
  signal waits; each `struct dcmsg` lives on the held Task's stack,
  which is never reused.

  The timeout: the crash process cannot watch a call it is itself
  blocked in, and the call that can block is the window open inside
  `BuildEasyRequestArgs`, so a disposable requester process makes that
  call and the crash process waits for its signal or a 2-second
  timer.device request, whichever comes first. The requester process
  gets its `struct dcreq` (`AllocMem`, 28 bytes) as a message on its
  process port; who frees it is decided under `Forbid`: the crash
  process while the requester is wanted, the requester process once the
  crash process has set `r_Gone`. Polling `SysReqHandler` alone could
  not time out, since the build is what blocks; the answer itself has no
  timeout, as on the Amiga.

  Costs: 3,232 bytes of code (the stub included), 450 of data (strings,
  the `EasyStruct`, the port) and 50 of BSS in the dos.library hunk
  (code 38,822 to 42,054 bytes), no kernel text (53,244 bytes) and no
  fixed stack-segment reservation. Pool costs at run
  time: the crash process's 3,072-byte stack once intuition has shown a
  crash, a requester process's 2,560 bytes while its requester is up
  (for good if its window never opens), and each held Task's own stack
  and memory. wb900 measured the requester's `BuildEasyRequestArgs`,
  `SysReqHandler` and `FreeSysRequest` at 1,656-1,782 bytes over its
  screen modes, looks and fonts, which left 10 bytes of a 2 KB stack
  above the 256-byte interrupt headroom; `DisplayAlert` takes 2,120
  bytes when it must open its own screen (1,336 on an open one), before
  the crash process's own frames (a 143-byte text buffer among them). `tests/crashnoint.c` (no intuition), `tests/crashint.c`
  (fake intuition: Suspend, no window, timeout, `input.device`, turns,
  Reboot), `tests/crashforbid.c`, `tests/crashdisable.c` and
  `tests/crashirq.c` (dead ends) check it.
- **Processes** (`dos/dosproc.c`). `struct DProc` is the `Process`
  followed by dos.library's entry pointer and a copy of the name, one
  `AllocEntry` with the stack, so exec's `RemTask` frees both. The Task
  starts in `dprocent`, which calls the entry through `dcall`
  (`dos/dosasm.s`): `dcall` saves r6-r13 and stores the stack pointer in
  `pr_ReturnAddr`, so `Exit` (`dexitto`) can return from any depth.
  `dprocend` then releases what `pr_Flags` says and returns into exec's
  finalizer. CLI numbers are the lowest free bit of a 32-bit map.
- **RunCommand** allocates the stack, then `StackSwap`, `dcall`,
  `StackSwap` back, all in one C function: compiled code addresses its
  frame through r13 and pops arguments with `inc r15`, so the calls
  between the two swaps push on the new stack while the function's own
  locals stay on the old one, and its epilogue (`ldm` from the stack
  pointer) runs after the swap back. `pr_Arguments`, `pr_ReturnAddr` and
  `pr_StackSize` are saved and restored around the command.
- **StackSwap** (kernel, `platform/trap.s`, 82 bytes of text) exchanges
  the three values with interrupts off and returns on the new stack,
  copying the argument slot and return address there for the caller's
  pop. It knows `tc_SPLower`'s offset (46); `tests/dosproc.c` checks it.
  It is the only kernel change for DOS: Amiga's RunCommand is built on
  it, and a private stack switch in dos.library would have had to update
  the Task's bounds behind exec's back.
- **Packets and names** (`dos/dospkt.c`, `dos/dosio.c`). A packet is a
  `StandardPacket` on the caller's stack (68 bytes). `GetDeviceProc`
  (`dos/doslist.c`) resolves the device part under `di_DevLock` held
  shared; starting a handler and binding a late assign drop it, since
  both send packets and a handler adds its volume under the lock held
  exclusively. `di_EntryLock` serialises handler starts and the shell's
  load.
- **Assigns and residents** (`dos/dosext.c`). A multi-directory
  assign keeps its first lock in `dol_Lock` and the rest in
  `dol_List`. `GetDeviceProc(name, dvp)` finds the entry after
  `dvp_Lock` under the DosList lock held shared, after checking that
  `dvp_DevNode` is still listed; the name calls loop on it
  (`dnamepkt`, `xOpen`) and keep the last directory's error, so a
  missing name reports `ERROR_OBJECT_NOT_FOUND` rather than the end of
  the list. Replacing or removing an assign unlocks its list. The
  resident list's head is `di_ResList` (`di_McName`, as on the Amiga);
  `AddSegment`, `FindSegment` and `RemSegment` walk it under `Forbid`.
  A new CLI's command path is copied by `dpathdup` in `CreateNewProc`
  and freed by `dpathfree` when its process ends.
- **LoadSeg** reads through a 512-byte buffer, because exec's loader asks
  for single longs and each `Read` is a packet round trip. A read of 512
  bytes or more that finds the buffer empty goes straight to `Read`, so
  a hunk costs no copy.
  `SysBase + LVOAllocMem` and `+ LVOFreeMem` are the funcarray's
  allocator: exec's vectors take no base.
- **Execute and SystemTagList** create the shell process under `Forbid`,
  so a SystemTagList shell's `cli_CurrentInput` is cleared (run only the
  argument line) before it runs; a synchronous caller waits on a signal
  of its own that `NP_ExitCode` raises with the result.
- **Buffered I/O** (`dos/dosbuf.c`). `AllocDosObject(DOS_FILEHANDLE)`
  makes a `struct DFH` (`dos/dosint.h`): the `FileHandle`, then the
  buffer's size, flags (write state, owned buffer, end seen, end pushed
  back) and `SetVBuf` mode. In read state `fh_Pos`..`fh_End` is
  read-ahead; in write state `fh_Pos` bytes are pending and `fh_End` is
  0, so one comparison (`fh_Pos >= fh_End`) sends `FGetC` to the refill
  path in both. `FPutC` and `VFPrintf`'s output procedure store a byte
  directly while it cannot fill the buffer or end a line; anything else
  goes through `dbwrite`. `Read`, `Write`, `Seek` and `Close`
  (`dos/dosio.c`) settle the buffer before their packet; `Seek` folds a
  file's unread read-ahead into an `OFFSET_CURRENT` seek, one packet.
  `VFPrintf` no longer needs a stack buffer: `RawDoFmt` writes into the
  handle's.
- **ReadArgs** (`dos/dosargs.c`) parses in one block on `RDA_DAList` (or
  `RDA_Buffer`): the `/M` pointer array (half the line's length plus 2
  slots), the `/N` longs, a flag byte per template item, then the
  strings, at most the line's length plus 2 bytes. Each word is read with
  `ReadItem` straight into the string area; keywords are looked up with
  `FindArg`; a positional word goes to the first item that is neither
  given nor /S, /T or /K, and /M keeps taking them. The `?` line is a
  256-byte block on the same list. `FreeArgs` frees the list, and the
  `RDArgs` itself when ReadArgs made it (a private `RDA_Flags` bit).
- **Patterns** (`dos/dospat.c`). The parser wraps every operand of `~`,
  and of `#` unless it is one character, in a group, so the matcher sees
  only one-character elements and groups. The matcher is recursive with
  continuations (`struct dk` on the stack: where to go after a group,
  and for a repetition where its pass started, so an empty pass fails):
  it recurses at `#?`, `#`, `~` and each alternative, never per
  character of a run, so its depth follows the pattern's operators.
  `MatchFirst` locks the wildcard-free prefix and makes an `AChain` per
  later path part; `an_String` is the prefix (first level) or "", then
  the part's parsed pattern or literal name. A literal level locks its
  name (`dlockin`: `ACTION_LOCATE_OBJECT` relative to the level's lock);
  a pattern level walks its directory with ExNext in `an_Info`. A level
  added for `APF_DODIR` has `DDF_AllBit`, is freed when its directory is
  done, and makes the parent's entry come back with `APF_DIDDIR`.
- **Variables** (`dos/dosvar.c`). A `LocalVar` is one `AllocVec`: node,
  name, value and a NUL; `SetVar` builds the new one, inserts it after
  the old one and frees that, so a failed allocation leaves the old
  value. dos.library's global list has a semaphore (shared for
  `GetVar`). Whether ENV: exists is a `FindDosEntry` under the DosList
  lock, each call: an assign can appear or go at any time.
  `CreateNewProc` copies the creator's list before `AddTask`;
  `dprocend` frees it.
- **CON:** (`dos/con.c`). The handler keeps one textcon.device read
  outstanding (32 bytes) and edits each byte as it arrives into a
  256-byte line; finished lines go to a 512-byte ring that `Read`
  packets drain. Ends of input are queued positions in that stream, so
  type-ahead such as `q` Ctrl-\ Ctrl-\ gives "q", 0, 0. Echo and writes
  are synchronous `CMD_WRITE`s on a second request.
- **RAM handler** (`dos/ram.c`). Upstream's `lib/ram` (one contiguous
  buffer per file, doubled as it grows, 8.3 names, a 256-node table)
  was read for policy; what applies is kept: deleting or truncating
  frees storage, a failed allocation gives `ERROR_NO_FREE_STORE` and
  leaves the file usable, a write may store a prefix, Flush succeeds.
  The rest follows AmigaDOS's ram-handler. Nodes are `AllocMem`'d with
  their name inline (30 bytes); a directory's entries are a list kept in
  creation order, so `ExNext` with the last entry's address
  (`fib_DiskKey`) is one pass. A file is a chain of chunks, each a
  `MEMF_ANY` block of an 8-byte header and 64 to 8,192 bytes, sized
  like the file so far; a seek walks the chain, at most about 130 links
  for a megabyte. Chunks are cleared when allocated and an extension's
  bytes are zeroed, so no old data shows through. Uses are counts in
  the node (`n_Use`, `n_Excl`); handles are also on a list so
  `SetFileSize` can pull positions back. Dates come from `DateStamp`
  (the system time) on create, close after a write, and the parent's
  on each entry added or removed.
- **V7 handler** (`dos/v7fs.c`). A lock's `fl_Key`
  holds its inode and its directory's inode, because a V7 inode has no
  name and a file has no "..": `Examine` finds the name in that
  directory. Locks chain through `fl_Link` and file handles through
  their own list; both count as uses of their inode. A file handle keeps
  only its key and position and reads the inode for each packet, so
  handles on one file see each other's writes. Allocation follows
  COHERENT: blocks pop from the superblock's `s_free` stack, which a
  chain block refills, and a freed block spills a full (or empty) stack
  into itself; inodes come from `s_inode`, refilled by scanning the
  inode blocks. A file's blocks are freed last first, so a new file gets
  them back in their old order. Reads and writes go through the cache
  and step the caller's buffer with `PtrStep`, so one `Read` may fill a
  block over 64 KB. `ACTION_SET_PROTECT` and `ACTION_SET_DATE` rewrite
  the inode with `keepm` set, so `iput` stores the inode's own modification time
  (the new date, or the old one for a protection change) and stamps
  only the change time; every other `iput` stamps both.
- **V7 volumes.** One seglist serves `DH0:` and `DH1:`, as an Amiga
  filesystem's does: each handler process allocates a `struct v7` at
  startup (the volume's cache, superblock, entry and name sizes, locks,
  handles, inhibit and die state, and its wd.device and timer.device
  requests) and passes it to every function that needs it; the hunks
  hold only `DOSBase` and constant data. The cache functions copy the
  fields their loops use into locals and test the last buffer found
  before calling `cfind`, since cc2 reaches a field through a pointer
  with more instructions than a global: `tests/fsbench` runs 0.3% more
  instructions than with globals (4,018,601 against 4,006,643).
- **V7 cache.** One allocation holds the superblock and the
  `de_NumBuffers` buffers, placed on a 512-byte physical boundary so that
  wd.device DMAs them directly, several sectors per command; a slot
  table beside it holds each buffer's block, its write class and a
  reference bit. Replacement is a clock: the hand passes over buffers
  used since its last pass, so the inode and indirect blocks every packet
  touches stay, and buffers taken in turn are adjacent in memory. A
  changed buffer under the hand is not written alone: `bsync` writes
  every change, class by class (file data, then the superblock, single
  and double indirect blocks, inodes, directory blocks), each class in
  block order, and adjacent buffers holding adjacent blocks go in one
  transfer, so a sequential write leaves as runs of up to the cache's
  size. A data block past the end of the file is new and is not read
  first; a whole-block write is not either. A read miss on file data
  takes up to `RAMAX` (8) following file blocks that are contiguous on
  the disk and not cached into the buffers after it, in one `CMD_READ`,
  and moves the hand past them. The class order makes each on-disk
  pointer reach the disk after what it points to; the operations that
  would otherwise invert it write early: a new directory block is
  cleared on the disk at allocation (its inode is written before its
  entries), a new directory's `.` and `..` block before its name, a chain
  block of the free list (spilled into a freed block) at once, the
  superblock as soon as it takes over a chain block's list (before that
  block is reused), and Rename's new entry before the old is cleared.
  Delete and truncation clear the entry (`bsync`), then the inode
  (`bsync`), then free the blocks and write the superblock (`irelse`), so
  a block is free on the disk only once nothing there points to it. The
  time stamps come from `DateStamp`, the system time. The idle flush is a `TR_ADDREQUEST` of one second, sent while
  changes wait and re-sent while packets keep coming, until a second
  without packets or 5 seconds since the first change.
- **Shell** (`dos/shell.c`). Every CLI process runs the one
  `rn_ShellSegment`, so the shell keeps nothing in its data or bss: its
  state (`struct sh`: the CLI, the input stack, the line buffers) is an
  `AllocMem` block that `pr_ShellPrivate` points at, and each function
  that needs it fetches it once (`shget`, one `FindTask`). The macros
  `cli`, `top`, `line`, ... name its fields. Inputs are a stack: the
  standard input (`struct inhead`, a 256-byte read-ahead buffer and the
  If/Skip state) at the bottom, and a `struct inp` per `Execute`d script
  (about 1,000 bytes: buffer, arguments, a 256-byte pool for the `.key`
  template, argument words and defaults, the Skip label). A script line
  is substituted into a second buffer before it runs. If and Skip
  only set the top input's skip state; skipped lines are read and
  matched (If/EndIf nesting, Else, Lab, EndSkip) without running.
  `Skip BACK` seeks the script to 0 and drops the read-ahead. Built-ins
  are a name table and a `switch`; a user resident entry of the same
  name wins, as AmigaOS looks up its resident list first. Commands get
  `pr_CIS`/`pr_COS` set to the standard streams or the redirection; the
  shell flushes that output after each command, its own before it
  prompts and at its end (dos.library's output calls are buffered).
  `$` substitution and alias expansion write into whichever of the two
  line buffers the line is not in; the ReadArgs built-ins set
  `pr_Arguments` to their argument string around the call
  (`SetArgStr`).
- **C: commands** share `dos/c/cargs.h` (ReadArgs with the error
  reported, string copy, fault report, Ctrl-C) as static functions,
  so a command carries only what it calls. `Delete ALL` reads a
  directory one entry ahead and deletes the entry behind, which works
  on both handlers' `fib_DiskKey`; `Copy ALL` and `Delete ALL` recurse
  with their scan state (`FileInfoBlock` and paths, 600-700 bytes) in
  `AllocVec` blocks, not on the command stack.
- **History** (`dos/con.c`). Entered lines go into a 512-byte buffer,
  each ending in a NUL; when a line does not fit, the oldest go. Cursor
  up and down move an index through it and redraw the line with
  backspaces.
- **Sizes** (hunk code + data + bss bytes, heap): `dos.library`
  31,886 + 2,094 + 284 (ReadArgs, patterns, variables and buffering
  take 14.4 KB of the code: `dosargs.c` 3,750, `dospat.c` 4,756,
  `dosvar.c` 2,714, `dosbuf.c` 3,282, `dosio.c` +526); `con-handler`
  3,176 + 164 + 1,372; `v7fs-handler` 16,180 + 420 + 4, and per running volume its cache
  9,216 + 96 with 16 buffers and 1,928 more; `ram-handler`
  6,500 + 128 + 108; `L:Shell-Seg` 15,602 + 1,584 + 4 (12,650 before
  the variable built-ins, substitution and ReadArgs; and an 800-byte
  state per shell); `C:Copy` 3,032 + 158 + 536, `C:Delete` 1,814 + 102
  + 350, `C:Rename` 1,178 + 92 + 520, `C:MakeDir` 950 + 72 + 4,
  `C:Assign` 2,416 + 306 + 112, `C:Protect` 1,428 + 84 + 264, `C:Info`
  1,816 + 272 + 424, `C:Version` 1,824 + 180 + 704, `C:Date` 2,516 +
  304 + 8, `C:Wait` 1,292 + 74 + 4, `C:Dir` 2,084 + 142 + 266, `C:Type`
  1,980 + 106 + 260; on cargs.h's word splitting they were 3,602,
  2,142, 1,500, 1,142, 3,024, 1,786, 1,904, 2,148, 2,288, 1,608, 1,048
  and 882 bytes of code. Each hunk also takes 8 bytes of loader header.
  A handle's buffer (256 or 512 bytes) is heap from its first buffered
  call to `Close`; a `LocalVar` is 24 bytes plus its name and value.

## C library

The contract is [C library](platform.md#c-library).

- **Source.** `commodore-900-toolchain/src/libc` is complete source
  (`gen`, `stdio`, `crt`, `sys`), BSD 3-Clause. The toolchain's own
  `libc-z8001.a` cannot be linked: its C objects are compiled with
  `cc2 0010`, without the `SS` frame relocation Exec900 stacks need, and
  its `sys` members trap into COHERENT. So the Makefile compiles the
  sources itself with `0012` and the toolchain headers, assembles the
  `.s` members (they reach the stack through `SS`, which `progabs.o`
  supplies) and archives them with the toolchain's `mkarz` and the
  `libc/` objects. `ld` rescans an archive until nothing new resolves,
  so member order does not matter.
- **Replaced.** `LIBCSKIP` drops the sources `libc/` replaces or that
  cannot work: the system-call stubs (generated in the toolchain, never
  compiled here), `brk`, `sbrk`, `signal`, `exit`, `_finish` (a no-op
  that would shadow stdio's), `abort` (`halt`, which would stop the
  CPU in system mode), `isatty`, `getwd`, `getcwd`, `mkdir` and
  `system` (`fork` and `/bin`), `sleep` (`alarm`), `getenv`
  (`libc/env.c`, over `GetVar`), `strerror` and `perror`
  (`libc/errstr.c`: `sys_errlist` plus `ENOSYS`; COHERENT's `strerror`
  carried a second, 1,310-byte table), the MBLOCK `malloc`,
  `realloc` and `notmem`, the profiler and the no-float `sdtoa` stub
  (built as `nofloat.o`). Every other COHERENT system call has an
  `ENOSYS` stub in `libc/nosys.c`, so programs naming them link.
- **Exit.** `cstart` calls `setjmp`; `_exit` stores the status and
  `longjmp`s back, so cleanup is in one place and the status returns
  through `RunCommand` like any command's. DOS's `Exit` would skip
  closing dos.library. Optional parts register through globals
  `cstart` tests: `_exithk` (`atexit`), `_mpool` (`malloc`), `_cwdlk`
  (`chdir`), so a program links only what it uses. `exit` always links
  stdio's `_finish`.
- **malloc** uses an exec pool rather than an `sbrk` emulation: MWC's
  allocator stops growing for good after one failed `sbrk`, and
  `DeletePool` returns every block at exit, which a program in the
  shared heap needs. The size lives in a long before each block for
  `FreePooled`. 65,500 bytes plus that long and the pool's 16-byte
  large-block link stays under 64 KB, so a block never crosses a
  segment.
- **Descriptors** keep the DOS handle and three flags. The standard
  ones are the shell's streams, which the shell reuses, so neither
  `close` nor exit closes them. `_doserr` maps `IoErr()` through a byte
  table (codes less 100), about 100 bytes smaller than a `switch`.
- **stdio** keeps COHERENT's buffering. On the emulator (100 Hz ticks,
  `SYS:` files), 20,000 bytes took 119 ticks through `putc` and 200
  through `FPutC`, reading them 42 through `getc` and 93 through
  `FGetC`; 1,000 `fprintf` lines took 76 ticks, `VFPrintf` 123; 1,000
  25-byte lines 143 through `fputs`, 151 through `FPuts`. The `putc` and
  `getc` macros stay in the program, while each DOS call goes through
  the library vector and its segmented frame.
- **Shell.** The AmigaDOS `*` escape inside quotes made the shell's
  redirection scan end a quoted word early (`"a*"b"`); `redir` now
  skips `*x` there, as AmigaDOS does.
- **Sizes** (code + data + bss): `cstart.c` 884 + 32 + 46, `fd.c` 1,580
  + 50 + 122, `stat.c` 1,224, `file.c` 578 + 4 + 256, `nosys.c` 694,
  `malloc.c` 394, `time.c` 204, `atexit.c` 136 + 4 + 130, `system.c`
  70, `env.c` 292 + 0 + 4, `errstr.c` 188 + 68; COHERENT's `printf.c` 2,146 + 32, floating-point formatting
  (`dtefg.c` and the soft-float routines) 2,984 + 220 + 38 in a program.
- **Doubles.** `libc/fpepu.s` takes the names of the runtime's
  `dr`/`dl` add, subtract, multiply and divide (by value / second
  operand by address). It tests `AttnFlags` (ExecBase offset 34) per
  call, about 40 clocks, and runs the operation on the EPU or jumps to
  the toolchain routine, assembled from `crt/dadd.s`, `dmul.s` and
  `ddiv.s` with its names prefixed `fs`. The compiler has no EPU code
  generation (its `VEPU` variant is unimplemented), so the runtime is
  the cheap place to use the EPU. mathieeedoubbas's `Floor` and `Ceil`
  are built on `modf`.
- **Stack.** No fixed stack-segment reservation: a C program runs on its
  command stack. `CTest` (stdio files, `qsort`, `printf` with floats)
  measured 816 bytes, interrupt frames included.

## Kernel source map

| File | Upstream source | Contents |
| --- | --- | --- |
| `platform/lists.s` | `lib/exec/execlists.act` | Lists (hand-written Z8000) |
| `kernel/tasks.c` | `lib/exec/taskpolicy.act`, `platform/altirraos/tasks.s`, `preemptive.s` | Task admission and removal, scheduler (`kswitch`, `ktick`, `kready`, `kmaybe`), Forbid/Permit nesting panics, Disable/Enable, Yield, Sleep, FindTask, SetTaskPri, CreateTask, idle and final, trap dispatch (`kfault`, `TaskTrapCode`), stack probe, Alert |
| `kernel/signals.c` | `taskpolicy.act` (signals), `signal-atomic.s` | AllocSignal, FreeSignal, SetSignal, Signal, Wait |
| `kernel/ports.c` | `lib/exec/portcore.act`, `platform/altirraos/ports.s` | Message ports |
| `kernel/semaphores.c` | — (Amiga Exec V39) | Signal semaphores, `SemaphoreList` |
| `platform/semfast.s` | — | Uncontended Obtain, Attempt and Release (shared and exclusive), Forbid, Permit (hand-written Z8000) |
| `kernel/memory.c` | `lib/exec/heapcore.act`, `heappolicy.act`, `execmemory.act` | MemHeader allocator, AllocMem/FreeMem/AllocVec/FreeVec/AvailMem/TypeOfMem, AllocAbs, blocks over 64 KB, PtrStep, CopyMem, AllocEntry/FreeEntry, CachePreDMA/CachePostDMA, `kaddmem` |
| `kernel/pools.c` | — (Amiga Exec V39; `MEMF_SEM_PROTECTED` from AROS) | CreatePool, DeletePool, AllocPooled, FreePooled |
| `kernel/boot.c` | `lib/exec/bootconfig.act` | `cmain`: console, memory, root/idle Tasks, interrupts, resident devices, tick, boot modules, then the root program (`krun`) |
| `kernel/interrupts.c` | — (`platform/altirraos/interrupt-work.s` is upstream's fixed counterpart) | `kintr` dispatch and exit, SetIntVector, AddIntServer, RemIntServer, IntStray, tick server |
| `kernel/devices.c` | `lib/io/iocore.act`, `lib/io/task-io.inc` | AddDevice, OpenDevice, CloseDevice, SendIO, DoIO, CheckIO, WaitIO, AbortIO, TermIO, CreateIORequest, DeleteIORequest, `ResModules` |
| `kernel/libraries.c` | — (Amiga Exec V39) | MakeFunctions, MakeLibrary, InitStruct, AddLibrary, RemLibrary (also RemDevice), OpenLibrary, OldOpenLibrary, CloseLibrary, SetFunction, SumLibrary, AddResource, RemResource, OpenResource, FindResident, InitResident, InitCode, exec.library's base, the unimplemented-vector alert |
| `kernel/loadseg.c` | — (Amiga dos.library V36; `docs/reference/program-loading.md` is upstream's o65 loader) | InternalLoadSeg, InternalUnLoadSeg |
| `kernel/bootmods.c` | — | Boot-volume COHERENT reader (`kbfinit`, `kbfopen`, `kbfread`, `kbfdone`, `BootFuncs`), `kbootmods`: boot modules and the root program |
| `kernel/testhooks.c`, `kernel/testhooks.h` | — | Test-only `TestHooks` vector and table |
| `dos/doslib.c`, `dosmisc.c`, `dospkt.c`, `doslist.c`, `dosio.c`, `dosext.c`, `dosproc.c`, `dosasm.s`, `fd/dos_lib.fd`, `include/dos/` | `lib/dos/dos.act`, `doscore.act`, `dos-packets.inc`, `dosobjects.act`, `dosprocess.act`, `process.act` (structure only; AmigaDOS V36 policy) | dos.library: processes, packets, DosList and assigns, files and locks, LoadSeg, RunCommand, Execute, SystemTagList |
| `dos/dosbuf.c`, `dosargs.c`, `dospat.c`, `dosvar.c`, `dosdate.c` | — (AmigaDOS V36/V39 autodocs) | dos.library: buffered I/O, ReadArgs, pattern matching, variables, dates and SplitName |
| `dos/con.c` | `lib/dos/cookedline.act`, `doscooked.act` | `con-handler`: CON: over textcon.device |
| `dos/v7fs.c` | `lib/fs/fshandler.act`, `fspacket.act`, `fslocks.act`, `fsdirectory.act` (structure only) | `v7fs-handler`: the boot volume's V7 filesystem, read and write |
| `dos/ram.c` | `lib/ram/*.act`, `docs/reference/ram-filesystem.md` (policy only) | `ram-handler`: RAM:, the Ram Disk volume |
| `dos/shell.c`, `dos/c/*.c` | `lib/dos/command.act`, `doscommand.act` | `L:Shell-Seg`; `C:Assign`, `Copy`, `Date`, `Delete`, `Dir`, `Info`, `MakeDir`, `Protect`, `Rename`, `Type`, `Version`, `Wait` |
| `lib/dstartup.s`, `lib/dosstart.c` | — (Amiga startup code, amiga.lib) | DOS program startup; `Printf`, `FPrintf` |
| `libc/*.c`, `libc/crt0.s`, `libc/include/errno.h` | — (COHERENT's system calls; the library itself is the toolchain's `src/libc`) | C library startup, exit and atexit, descriptors and files over dos.library, stat, malloc over a pool, time, system, `ENOSYS` stubs |
| `lib/startup.s`, `lib/amiga.c` | — (Amiga startup code, amiga.lib, debug.lib) | Program startup; `NewList`, `KPrintf` for programs |
| `platform/progabs.s`, `tools/mkhunk.py` | — | Absolute `SS` and `SysBase_` for separately linked code; l.out to hunk conversion |
| `fd/exec_lib.fd`, `fd/battclock_lib.fd`, `fd/framebuf_lib.fd`, `fd/gameport_lib.fd`, `fd/timer_lib.fd`, `tools/genlvo.py` | — (Amiga NDK fd files) | Vector order and C types; generated call macros and exec.library's jump table, `SysBase`, `kunimpl` and the `Alert` vector's label `kalertv` (`execvec.s`) |
| `devices/timer.c` | `lib/io/timerdriver.act`, `timernative.act` | timer.device, with the AmigaOS 3.x interface in place of upstream's |
| `devices/battclock.c` | — (`docs/reference/timer.md`: no wall clock) | battclock.resource: ReadBattClock, WriteBattClock, ResetBattClock, `kbattinit` |
| `devices/wd.c` | — (`docs/reference/ideplus-device.md`, `block-io.md`: contracts) | wd.device, `kwdinit` |
| `devices/serial.c` | — (`docs/reference/device-io.md`: contract; Amiga serial.device) | serial.device, `kserinit` |
| `devices/parallel.c` | — (`docs/reference/device-io.md`: contract; Amiga parallel.device) | parallel.device, `kparinit` (also CIO U66 setup) |
| `devices/beep.c` | — (`docs/reference/device-io.md`: contract) | beep.device, `con_bell`, `kbeepinit` |
| `devices/lc.c` | — | `lc.display`: LC card 8568 display backend, `vidsetup` |
| `devices/lr.c` | — | `lr.display`: LR card display backend, `vidsetup` |
| `devices/vt.c` | — (Amiga console.device escapes) | Display modules: terminal emulator, `vt_init`, `vt_putc`, `vt_write` |
| `devices/console.c` | — (`docs/reference/device-io.md`: contract; Amiga console.device, misc.resource) | Card probe and mapping (`kvidinit`), card module names (`kvidmod`), boot text, `kvidputc`, `kvidgetc`, textcon.device, framebuf.resource, `kconinit` |
| `devices/condisp.c` | — (Amiga console.device) | Display modules: the display half of textcon.device, draw queue, romtag |
| `devices/hr.c` | — | `hr.display`: HR card display backend, `vidsetup` |
| `devices/font8x16.c` | — | `hr.display`, and `lc.display` at 640x400: 8x16 font, ASCII 0x20-0x7E (`tools/mkfont8x16.py`) |
| `devices/font8x8.c` | — | `lc.display`: 8x8 font, ASCII 0x20-0x7E (`tools/mkfont8x8.py`) |
| `devices/gameport.c` | — (Amiga gameport.device) | `gameport.dev`: gameport.device, a boot module |
| `devices/lance.c` | — (Amiga SANA-II, a2065.device) | `lance.device`: the LANCE card's SANA-II driver, a boot module |
| `devices/keyboard.c` | — (`docs/reference/device-io.md`: contract; Amiga keyboard.device) | keyboard.device, `kbd_getraw`, `ksetlatch`, `kkbdinit` |
| `devices/conkeymap.c` | — (Amiga console.device's default keymap) | the console's keymap, `kbd_getc` |
| `kernel/kprintf.c` | — | `KPutStr`, `RawPutChar`, `RawDoFmt`, `KPrintf` (`%s %c %d %u %x %ld %lu %lx %p %%`) |
| `kernel/epu.c`, `platform/epuasm.s` | — (Amiga Exec's 68881 support) | EPU probe (`kepuinit`), lazy context switch (`kepuswitch`, `kepusave`, `kepuload`) |
| `math/mathieeedoubbas.c`, `math/mathieeedoubtrans.c`, `math/mathepu.s`, `fd/mathieeedoub*_lib.fd` | — (Amiga mathieeedoubbas and mathieeedoubtrans V39) | The math libraries, boot modules `ieeedoubbas` and `ieeedoubtran`; EPU code |
| `libc/fpepu.s` | — | The C runtime's double add, subtract, multiply and divide on the EPU |
| `platform/crt.s`, `trap.s` | `platform/altirraos/*.s` | Entry, PSA (every vector to one entry), trap and interrupt stubs, halt and reschedule |
| `platform/portio.s` | — | `inb`, `outb`, `outw`, the interrupt mask (`intoff`, `intrest`); programs and display modules link it too |
| `platform/mmu.s` | — | Z8010 descriptor read-back (`kmmubase`) |
| `platform/block.s` | — | Word and byte block moves and word fill (`kwmove`, `kbmove`, `kwfill`), word input (`inw`); also linked into display modules |
| `platform/glyphs.s` | — | `hr.display`: HR glyph runs (`kglyphs`) |
| `platform/machine.c` | — | SCC console, CIO tick, bootinfo memory map, identity heap, physical pool, windows, `kphys` |

## Tests

A test is `tests/<name>.c` defining `int main()`, built as a hunk program
([Program loading](platform.md#program-loading)): `lib/startup.s`, the
test, `lib/amiga.c`, `platform/portio.s` and `progabs.s`, linked with
`ld -r` into `$(OUT)/progs/<name>`. Its disk image holds the shared
`exec900.sys` with the test as the root program (`mkdisk.py --root`). It
runs as the root Task and prints `PASS <name>` or `FAIL <name>: <reason>`
on the console. When `main` returns, the kernel prints `exec900: root
exit <n>` and calls `emustop()`. `make -C c900 test T=<name>` builds the
program and its disk, boots it in the emulator and checks the
transcript. `make -C c900 check` first runs the host tool tests
(`make hostcheck`, Python `unittest` files `tests/host/test_*.py`), then
builds every image and runs every test; the kernel is linked once.

A program cannot link kernel symbols: mkhunk refuses an unresolved
external. White-box tests include `tests/test.h`, which includes
`kernel.h` for the kernel's types and macros and maps the kernel names
they use:

- Amiga ExecBase fields: `SysBase->LastAlert`, `MemList`, `LibList`,
  `DeviceList`, `PortList`, `TaskReady`, `TaskWait` and `TaskTrapCode`;
  `TickCount` is `SysBase->ex_TickCount`.
- `kphys(p)` is `CachePreDMA(p, &len, 0)`.
- The rest goes through the test-only `TestHooks()` vector (−858), which
  returns `struct TestHooks` (`kernel/testhooks.h`): pointers to the
  memory-map and window internals (`kmmutab`, `HeapPages`, `kalloc`,
  `kwinmap`, ...), driver counters (`WdStray`, `KbdIntrs`, ...) and
  the boot-volume reader.
- The display tests reach the display module through textcon.device's
  `CD_ASKDISPLAY` and `CD_SETDISPLAY` (`tests/condisp.h`); `vt` and
  `perf` link the terminal emulator (`devices/vt.c`) themselves.
  `test.h` defines each kernel name as an access through it, so test
  sources keep the kernel's names. The table is not an API: it changes
  with the kernel, and nothing but tests may call the vector.

`tests/kimage/NAME.c`, when present, is linked into a kernel image of
the test's own, `$(OUT)/NAME.sys`, which its disk boots instead of
`exec900.sys`. Only `tailpad` has one: 44 KB of bss that move the stack
segment's physical page boundary, which `tailmem` checks on the shared
image.

A test source may carry one-line directives, read by `tools/runtest.sh`:
`/* EMUFLAGS: --ram=08-35 */` adds emulator options (a memory
configuration, `--hr`), and `/* EXPECT: text */` passes on a transcript line
containing `text` instead of `PASS <name>`, for a test that ends in a
reported fault (`nullfault`). `/* FLOPPY: pattern */` attaches a fresh
pattern floppy image (`mkdisk.py --floppy`). `/* WIRE: 1 3 */` attaches
each named serial port to a loopback peer (`tools/echowire.py`) on a
socket in a new directory under `TMPDIR` or `/tmp`, since a socket path
holds about 100 bytes. `/* PRINTER:
text */` is read only by the Go model's runner, which attaches a printer
and requires `text` in its output.
`/* KEYS: TICK:CODE,... */` injects keyboard bytes on the Go model; the
emulator reports such a test as `SKIP` unless `/* EMUKEYS: T:HH,... */`
gives its `--key` codes, T loop ticks after the text of
`/* HWMARK: text */` is printed (`--hw-mark`).
`/* MOUSE: TICK:DX:DY[:LRM],... */` moves the HR mouse on the Go model
with `CARD=hr`; elsewhere the test runs without a card.
`/* BOOTS: N */` boots a copy of the image N times in a row, joining the
transcripts; a boot that rings the stop doorbell early is a reset
(`fscrash`, `lnames`). `/* FSCK: BASE... */` checks the V7 filesystem
at each block BASE of the image after each boot (`tools/cohfs.py --check`) and fails on
damage. The Go model and the core bench skip a `BOOTS` test.
`/* LANCE: loop */` fits the LANCE card on the Go model and the core
bench, and `/* LANCE: peer */` also a host on its cable (Go model only);
the emulator reports both as `SKIP`. EMUFLAGS
is split on blanks, so scripted `--input` must have none; serial tests
send it as `\i` type-ahead bytes released by `--input-mark`. In a
directive, `\^X` is the control byte X & 0x1F (`\^H` Backspace, `\^C`,
`\^\`), except `\^I` and `\^J`, which would split the words; the runners
substitute it before the emulator or simrun sees the option. Shared test
code lives in `tests/*.h`.

DOS tests boot a system volume: `DOSDISK` in the Makefile lists the DOS
modules, `L:Shell-Seg` and the `C:` commands (`dos/c/*.c`, built as
`$(OUT)/c/NAME`); `tests/cmds/NAME.c` are test commands (`$(OUT)/tc/NAME`),
`tests/libc/NAME.c` C library programs (`$(OUT)/tl/NAME`)
and `tests/sys/` holds script and text files a test `--put`s on the
volume. `DISK_<name>` in the Makefile holds a test's extra `mkdisk.py`
options (`stkcfg` boots with `--stack=48`, `tailmem` with `--stack=32`,
`rootstk` with `--rootstack=1` and `rootstk45` with both `--stack=45
--rootstack=1`).

The test disk (`tools/mkdisk.py`) has the boot partition at block 0, the
exec partition at 136 (also `bi_part[14]`, the boot volume; 1024 blocks
unless its files need more, [Graphics test boot disk](testing.md#graphics-test-boot-disk)),
and for the disk tests a pattern region in
`bi_part[0]` (blocks 1160–1223), a scratch region in `bi_part[1]`
(1224–1287) and the whole drive in `bi_part[15]`. Its `kboot.cfg` says
`console probe`, so kboot hands over the card it finds (`bi_console`) and
a display console runs alongside the serial transcript. The exec
partition holds the kernel, the test program, its `Startup` file and the
three display modules.
Hunk files come from `tests/hunks/NAME.c` as `$(OUT)/hunks/NAME`, with
any objects `HUNKOBJ_NAME` lists; the Makefile's `DISK_test` variable
adds them to that test's exec partition (`--module`, `--file` or
`--put`). `tests/split/` builds `bighunk`'s files over
64 KB with `mkhunk.py --split` from parts `genbig.py` generates, with
the results it expects in `bigexp.h`. The emulator writes
the image in place, so scratch contents persist between runs of one image.
A run ends when the root program returns, with no idle flush, so a test
that writes the boot volume closes, deletes or flushes what it changed
before it returns; changes left in the V7 handler's cache are lost and
leave blocks that are neither free nor used in the image.
