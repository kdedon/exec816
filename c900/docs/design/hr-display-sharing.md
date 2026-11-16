# Sharing the HR display (design)

Status: implemented as below, with the details under
[As built](#as-built); the contract is
[platform.md](../platform.md#framebuffer). Request 1 of wb900's
`EXEC_ASKS.md`: a graphics.library (wb900, ported from AROS) must own the
HR card's framebuffer, and textcon.device must stop drawing on it while
graphics has it.

## Today

- The kernel's half of textcon.device (`devices/console.c`) probes the
  card in `con_init` (`kvidinit`), derives the mode (`CDM_HR800`,
  `CDM_HR1024`, `CDM_HRCOLOR`) and maps the bitmap: HRSEG0 (logical 0x3E,
  identity) holds bytes 0-0xFFFF, HRSEG1 (logical 0x01, physical 0x3F)
  bytes 0x10000-0x1FFFF ([memory map](../platform.md#memory-map),
  [HR card](../hardware.md#hr-card)).
- `hr.display`, loaded by the kernel before `Modules`, holds the terminal
  emulator, the HR backend and gameport.device. It attaches with
  `CD_ASKDISPLAY`/`CD_SETDISPLAY` (private `struct ConDisplay`,
  `platform/display.h`) and then draws every console byte.
- gameport.device polls the mouse ports 0x0400/0x0402 from the 100 Hz
  tick. It is a second romtag in `hr.display` and asks the backend for
  the card (`hrcard()`).
- The card has no display start register, no sprite and no interrupt.

## Model

On AmigaOS graphics.library owns the display (`LoadView`,
`GfxBase->ActiView`) and console.device draws into a window's RastPort
through it; gameport.device reads the mouse counters itself, independent
of graphics. Exec900 gets there in two steps:

1. **Ownership protocol (this draft).** A kernel resource,
   `framebuf.resource`, describes the console card's bitmap and lends it
   to one owner at a time. Its name and structures are card-neutral: HR
   first, an LC 640x200 graphics backend later through the same calls. textcon.device is the owner by default; graphics takes
   it when it shows a View and gives it back when it shows none.
   gameport.device becomes its own module.
2. **Console through graphics (later).** Once graphics is running, console
   text is drawn by graphics into a screen or window, as on Amiga. The
   panic path keeps the polled HR backend, because graphics calls are not
   safe with VI off.

Ownership follows `misc.resource`'s `AllocMiscResource`/`FreeMiscResource`
convention (NULL on success, else the owner's name): the nearest Amiga
model for a hardware unit two drivers share. Amiga has no resource that
describes a framebuffer; the info call is a departure, recorded in
[platform.md](../platform.md) when implemented.

## Interface

`<resources/framebuf.h>` (MIT) and `fd/framebuf_lib.fd`, generated LVOs
as for battclock.resource:

```c
#define FRAMEBUFNAME "framebuf.resource"

#define FBC_HR    2             /* fbi_Card: the CDC_ numbers */
#define FBC_LC    3

#define FBF_MONO  1             /* 1 bit a pixel, MSB leftmost, 1 = lit */
#define FBF_IRGB  2             /* 4 bits a pixel, high nibble leftmost,
                                 * nibble bits 3-0 = I R G B */

struct FBMode {
    UWORD fbm_Width, fbm_Height;    /* HR: 1024x800, 1024x1024, 640x400 */
    UWORD fbm_Depth;                /* 1, 1, 4 */
    UWORD fbm_BytesPerRow;          /* 128, 128, 320 */
    UWORD fbm_Format;               /* FBF_ */
    UWORD fbm_SplitRow;             /* row crossing the 64 KB boundary,
                                     * 0xFFFF: none (HR mono) */
    UWORD fbm_SplitByte;            /* bytes of that row in half 0 */
    APTR  fbm_Half[2];              /* logical bases of bytes 0-0xFFFF and
                                     * 0x10000-0x1FFFF; NULL: not CPU-mapped */
    ULONG fbm_Size;                 /* 0x20000 on HR */
};

struct FBInfo {                     /* read-only, static for the boot */
    UWORD fbi_Card;                 /* FBC_ */
    UWORD fbi_NumModes;             /* entries of fbi_Modes */
    struct FBMode *fbi_Modes;
    UWORD fbi_Mode;                 /* current entry; HR: JP3, so 0 of 1 */
};

/* byte offset o of the bitmap -> address */
#define FB_ADDR(m, o)  ((UBYTE *)(m)->fbm_Half[(o) >> 16] + (UWORD)(o))
/* bytes of row y before the boundary: fbm_BytesPerRow unless y crosses */
#define FB_ROWSPLIT(m, y) ((y) == (m)->fbm_SplitRow ? \
                           (m)->fbm_SplitByte : (m)->fbm_BytesPerRow)
```

```
##base _FrameBufBase
##bias 6
GetFBInfo()() struct FBInfo *
AllocFB(name)(char *) char *
FreeFB()() void
##end
```

Vectors -6, -12, -18. `AllocFB` returns NULL when the caller now owns the
bitmap, else the owner's name; `FreeFB` gives it back to textcon.device.

- The resource exists only when the kernel probed and mapped a card with
  a bitmap (today HR); `OpenResource(FRAMEBUFNAME)` NULL means none.
- Task context only; neither call waits. The owner name is the caller's
  string and must stay valid while it owns the bitmap.
- The owner has the bitmap and the card's control port and serialises its
  own drawing. Blanking is the owner's: there is no call for it.
  `FreeFB` by anyone but the owner is a caller error (not checked).
- HR rows: a mono row never crosses (row 512 starts half 1:
  `fbm_SplitRow` 0xFFFF). Colour row 204 does, at byte 256 (x = 512):
  `fbm_SplitRow` 204, `fbm_SplitByte` 256. Row y starts at
  `FB_ADDR(m, (ULONG)y * fbm_BytesPerRow)`; a row operation does
  `FB_ROWSPLIT(m, y)` bytes there and the rest from `fbm_Half[1]`.
  Pointer arithmetic stays inside one half.
- No resource tracking: an owner that exits without `FreeFB` keeps the
  bitmap, as with misc.resource.

Kernel cost, estimated: 250-350 bytes of text (base, three vectors,
detach/reattach), about 40 bytes of data. No stack-segment change.

## Hand-off

Inside the kernel the resource is textcon.device's attach state
(`conon`), with one new hook in the private `struct ConDisplay`:
`cd_Reset()`, which `hr.display` fills in to re-run `dp_Init` (control
0x0F, clear 128 KB), reset the emulator and empty its queue.

| Event | Sequence |
| --- | --- |
| Boot | `kvidinit` probes and maps; the resource is added with exec's other kernel resources; owner `textcon.device`. Boot text goes to serial and the 1 KB `bootlog` ring as today. `hr.display` attaches and replays the ring. |
| `AllocFB(n)` | `Forbid`; owner not console: return its name. Else VI off, `conon = 0`, owner = n, VI on, `Permit`, return NULL. A Task draw holds `Forbid` for the whole write and interrupt-level output only queues, so no draw is in progress once the caller runs. The screen is left as it is: graphics clears or draws it. |
| Console output while lent | Serial line as today, and into the `bootlog` ring again (no new memory), so the latest kilobyte is kept. |
| `FreeFB()` | `Forbid`; `cd_Reset()` (clear: about 0.2 s, 64K words at some 20 clocks each); replay the ring; `conon = 1`; owner = console; `Permit`. |
| Panic, dead-end alert, trap (`kvidstop`) | If lent, the kernel takes it back without asking: VI off, `cd_Reset()`, `conon = 1`, then the message draws with `CDO_STOP` as today. Graphics never runs again. |
| Recoverable `Alert()` | Serial, and intuition's `DisplayAlert` when intuition.library is loaded and the caller is a Task outside `Forbid` and `Disable` ([platform.md](../platform.md#alerts)). |
| No HR card / LR or LC console | No resource; graphics gets NULL from `OpenResource`. |

graphics.library calls `AllocFB("graphics.library")` on the first
`LoadView` of a non-NULL View (intuition's first `OpenScreen`) and
`FreeFB` on `LoadView(NULL)` (last screen closed), so the
console is back when Workbench goes away. Init of graphics.library takes
nothing, as on Amiga.

## Mouse

gameport.device moves out of `hr.display` into its own module file
`gameport.device`, which the kernel loads after the display module
when the HR card is found (the same name table as the display modules).
`gpinit` finds the card with `OpenResource(FRAMEBUFNAME)` and
`fbi_Card == FBC_HR` instead of `hrcard()`. Behaviour, commands and the tick server stay as they are
([gameport.device](../internals.md#gameportdevice)). input.device
(wb900) opens it and reads `GPD_READEVENT`, as on Amiga. The mouse stays
usable whoever owns the bitmap, and textcon.device never used it.
Cost: `hr.display` shrinks by gameport.device; the new module carries its
own copy of the port helpers (tens of bytes).

## Pointer

There is no vertical blank and no sprite. As on Amiga, the pointer is
input.device's: the 100 Hz `INTV_TICK` server (`AddIntServer`, the
`INTB_VERTB` alias) only signals the input.device Task, which reads
gameport.device and moves the software pointer under graphics' drawing
semaphore, with Amiga-style hide and show around drawing that covers it.
Nothing draws at interrupt level; the resource has no pointer support.

## HR card facts for graphics

- **Control port** 0x0404, write: U312, a 74LS174 latching data bits
  5:0 on the rise of -SEL W (`C900_MiSTer rtl/hr_display/hr_u312.sv`).
  -SEL W is any I/O write with A1 = 0 in the card's 0x04xx block
  (`hr_u755.sv` line 32), so 0x0400, 0x0404, 0x0408 ... all load it:
  never write the mouse ports. Reset clears it (black screen).

  | Bit | Name | Effect |
  | --- | --- | --- |
  | 1:0 | BKS1, BKS2 | Bank select: latched, connected to nothing (`hr_card.sv` line 105). Write 11 |
  | 2 | MONO EN | Mono video on (0: black), `hr_card.sv` line 116. No effect in colour |
  | 3 | MONO I | Mono level: 1 full, 0 80 %, line 117. No effect in colour |
  | 5:4 | | Latched, unused |

  The owner may write 0x03, 0x07 or 0x0F at any time (blank, dim, full;
  `FBHR_BLANK`, `FBHR_DIM`, `FBHR_FULL`); console's reset writes 0x0F, the
  ROM and COHERENT 0xFF. No bit selects which memory is shown and there
  is no display start register: one 128 KB bitmap, no banking, hardware
  double buffering or scrolling.
- **Reads** (FN#7, the mouse interface; word reads). A1 = 0 (0x0400,
  0x0404) is X (U756), A1 = 1 (0x0402, 0x0406) is Y (U757): bits 15:13
  the buttons, 12:10 zero, 9:2 the axis counter, and bits 1:0 the
  quadrature phase with A2 low or a constant with A2 high: on X the JP3
  M/C strap, 10 mono and 01 colour (`hr_pal20x8.sv` lines 41-46), on Y
  00. There is no ID register. In the core the strap is an OSD option
  (`rtl/c900_core.sv` lines 412-416), and it reads 10 for both mono
  resolutions (the card's documentation gives 3 for 1024x1024, PLEB
  p.125), so kboot's `BF_HR1024` sets the line count. The read decode
  and PAL equations are inferred (the PALs are undumped), the strap
  least certainly; `hrstrap` in `devices/console.c` is the one table
  mapping it, and `mkdisk.py --hrmode` overrides it.
- **Access cost.** Every bitmap access waits for the card's CPU slot:
  2 to 5 wait states, 4.2 on average (the Go model's
  `TestFN2WaitStatesPerMode`, 8000 accesses at every clock phase, same in
  every mode, display or blanking), so about 7 clocks (1.2 us at 6 MHz)
  against 3 for a DRAM access, which has none. An LDIR copy within the
  bitmap measures about 20 clocks a word ([HR display](../internals.md#hr-display)).
  Keep off-screen work in RAM and copy whole rows.
- **Byte order.** The bus is big-endian. Mono: bit 15 of a word is bit 7
  of its even byte, the leftmost pixel (pixel (0,0) is bit 7 of physical
  0x3E0000), so byte-wise blits work. Colour: each byte holds two pixels,
  the high nibble leftmost, nibble bits 3-0 = I, R, G, B (plane I takes
  bits 4k+3; FN#6 shifts MD15-12 first).

## What wb900 implements

- graphics.library: `OpenResource(FRAMEBUFNAME)`, `GetFBInfo()`;
  BitMaps on the framebuffer built from `fbm_Half[]`, row by row (a
  plane is not one flat pointer: 100 KB spans two segments), splitting
  at `FB_ROWSPLIT`; a chunky 4-bit BitMap for `FBF_IRGB`;
  `AllocFB`/`FreeFB` around `LoadView`; control port writes only while
  owner.
- input.device: the tick server signals its Task, which moves the
  pointer under graphics' drawing semaphore.
- Nothing for gameport.device beyond opening it.
- Step 2 (later): console rendering through graphics (below).

## Testing

- Emulator, every card: `tests/fbres.c` passes with the resource absent;
  with a card it also checks the description and the ownership refusals.
- Go model, `CARD=hr` (`simtest`): `tests/fbshare.c` checks the info
  fields for the mode (split row included), Alloc returns NULL then the first owner's name,
  console output while lent reaches serial and leaves a bitmap checksum
  unchanged, Free clears the bitmap and the screen shows the replayed
  text (`hrtext.py`).
  A panic-while-lent test (`EXPECT`) checks the panic text on the screen.
- `tests/gameport.c` unchanged against the standalone module;
  `tests/hrdisplay.c`, `hrscroll.c`, `bootlog.c` unchanged.
- `make check` on the emulator; sizes of kernel text, `hr.display` and
  `gameport.device` reported.
- Later, board: 1024x800 visual check of a hand-off and return; colour
  and 1024x1024 remain untested until a card is set to them.

## Step 2: console through graphics

Two shapes, to decide later:

- a display module in wb900 implementing the backend contract with
  graphics calls (`Text`, `ScrollRaster`, `RectFill`) into a screen,
  attached with `CD_SETDISPLAY`; the contract (`platform/display.h`)
  would become a public MIT header; or
- wb900's own console.device for windows (AROS's, `CON:` in a window, as
  on Amiga), with the kernel's textcon.device kept as the system console
  that only draws when graphics does not own the display.

Either way the panic path stays on the polled HR backend.

## Decisions (wb900 review)

1. The resource lives in the kernel.
2. Return to console replays the 1 KB ring; no shadow cell buffer.
3. Two heads (LR or LC console, HR graphics) are low priority; this core
   support comes first.
4. Recoverable `Alert()` is serial only while lent; later intuition
   registers a hook so `Alert()` calls its `DisplayAlert`. Dead-end alerts
   keep the reclaim path. As built (later), there is no hook: as on the
   Amiga, the `Alert` vector finds intuition.library on the library list
   itself. dos.library carries that patch, not the kernel.
5. Blanking is the owner's only.
6. A separate resource, not a `misc.resource` unit.

## As built

Choices the draft left open:

- **Modes.** One `FBMode` per card, the current mode (`fbi_NumModes`
  1); a static table in textcon.device holds HR 800, 1024 and colour,
  then LC. `fbm_Size` is the bitmap memory from offset 0: 0x20000 on HR,
  0x10000 (the 8568's RAM) on LC.
- **LC.** The resource exists for an LC console too, describing the
  8568's 640x200 mono bitmap with `fbm_Half` NULL. AllocFB on LC stops
  the console from drawing; there is no LC drawing path yet.
- **Ring.** `AllocFB` empties the ring, so the return replays the text
  printed while lent (its latest kilobyte); earlier screen contents are
  not restored. A crash while lent replays it too, before the message.
- **Race.** `kvidputc` takes `Forbid` around its `conon` test and draw
  for a Task-level byte, so a Task preempted between the test and the
  draw cannot draw after `AllocFB`.
- **Repeat calls.** `AllocFB` by the owner returns its own name;
  `FreeFB` while textcon.device owns the bitmap does nothing.
- **Control values** are `FBHR_CTL`, `FBHR_BLANK`, `FBHR_DIM` and
  `FBHR_FULL` in `<resources/framebuf.h>`; `hr.display` writes
  `FBHR_FULL`.
- **gameport.device** is the boot module file `gameport.dev` (V7 names
  hold 14 bytes), loaded after `hr.display` from a second per-card name
  table; it links its own `inw` (`block.s`) and port helpers.
- **Attach while lent.** `CD_SETDISPLAY` 1 while lent records the
  attachment; output reaches the module at `FreeFB`.

Measured on the default image: kernel text +516 bytes (51,858 to
52,374), data +210, bss +68; `hr.display` code 11,246 to 8,552 bytes;
`gameport.dev` 3,162 code, 192 data, 8 bss; `lr.display` and
`lc.display` +114 code for `cd_Reset`.

## Open questions

- How an LC graphics backend draws: the 8568's VRAM is reached through
  its registers, not mapped (`fbm_Half` NULL), so it needs its own
  drawing path.
- The shape of step 2 (above).
