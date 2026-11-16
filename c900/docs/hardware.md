# C900 hardware reference

Facts the Exec900 drivers depend on. Sources: the C900_MiSTer RTL, the
COHERENT kernel port (`commodore-900-coh-kernel3`), kboot's `bootinfo.h` and
`memmap.h`, and the C900 emulator. Where they disagree, the RTL wins.

## Physical memory

Physical addresses are 24 bits; a physical segment is 64 KB (`A23:A16`).

| Segments | Contents |
| --- | --- |
| 0x00, 0x04 | Boot ROM, 32 KB, mirrored |
| 0x01-0x03, 0x05-0x07 | Not decoded |
| 0x08-0x0F | DRAM bank 0, always fitted |
| 0x10-0x17 | DRAM bank 1 (absent on 512 KB machines) |
| 0x18-0x35 | RAM card, 3 MB option (MiSTer only) |
| 0x18-0xFE | RAM card, 16 MB option (MiSTer only), less the holes below |
| 0x37 | LR text RAM, 4 KB mirrored through the segment |
| 0x3E-0x3F | HR bitmap, 128 KB; without an HR card, 0x3F is the ROM's stack RAM |
| 0xFF | Never RAM; also where untranslated cycles land |

The ROM's memory test cannot tell RAM from a framebuffer. kboot subtracts
`memmap.h` and hands the kernel a list of RAM runs (`bi_ram`, bootinfo v5).
The kernel allocates only inside those runs.

The stock BIOS stops its upward memory probe at the first segment that does
not answer. On a 16 MB RAM card without an LR board that is the hole at 0x37,
so RAM above it is not reported until the BIOS is updated.

## MMU

- There is one Z8010, reached by special I/O at port `0x00FC`, with the
  command in the high byte of the port.
- It has 64 descriptors and translates logical segments 0x00-0x3F only. A
  logical segment 0x40-0x7F gets no translation and lands at `0xFFFC00`:
  reads return `0xFFFF`, writes are lost, and no trap is raised.
- **At most 4 MB is mapped at once. Physical RAM beyond that is reachable only
  by reloading a descriptor.**
- Base addresses are in 256-byte units, but the board passes A9:A8 straight
  through. Bases must be 1 KB aligned.
- Attribute bits: `0x01` read-only, `0x02` system-only, `0x04` CPU-inhibit
  (an access raises the segment trap), `0x20` down-growing.
- The violation type is at `MMU+0x0200` and is reset by `MMU+0x1100`.
- Segmented pointers must keep bits 16-23 zero.

## DMA

The 8716 PDMAC bypasses the MMU and takes 24-bit physical addresses from a
command block in RAM. Its word counter has no carry into the address latch, so
a transfer wraps inside its 512-byte-aligned window. Disk buffers must be
512-byte aligned, or the driver bounces them one sector at a time.

## Interrupts

- **VI** (vectored) has one daisy chain:
  APU, CIO U70, SCC U74, CIO U66, PDMAC, backplane, LR SCC U31, LR SCC U36,
  then LANCE.
- Each chip supplies its own vector. Use even vectors only: the Z8001 indexes
  the PSA with the raw identifier. The chips drive only AD7-AD0: the
  identifier's high byte floats (0xFF on the board, 0x00 in the models),
  so only its low byte is the vector.
- **NVI** (non-vectored) carries single-step (latch `0x0203`) and the LC
  card's 8568 INTR. The 8568 holds INTR from power-up, and from every
  Update-Ready rise, until its status port `0x0181` is read.
- **NMI** is tied off.

| Vector | Source (COHERENT's assignment) |
| --- | --- |
| 0x00 | CIO U70 counter/timer 3, the 100 Hz tick |
| 0x08 | CIO U70 port A, keyboard |
| 0x10-0x1E | SCC U74, status in vector (WR2 = 0x10, WR9 VIS) |
| 0x70 | CIO U66 port B, printer ACK |
| 0x80 | PDMAC disk/floppy completion (fixed) |
| 0x88 | LANCE card, strapped on the card ([LANCE card](#lance-card)) |

## Devices

| Device | Ports | Notes |
| --- | --- | --- |
| CIO U70 (Z8036) | 0x0000-0x007F, register r at `2r+1` | CT3 tick; port A keyboard (PA7 ready); port B RTC data and Centronics data; PC0 printer BUSY, PC1 RTC CS, PC2 key up/down, PC3 keyboard clear |
| SCC U74 (Z8030) | 0x0100 (line 0, channel B, ROM console), 0x0120 (line 1, channel A); register n at `base+2n+1` | PCLK 6 MHz; TC = PCLK/(32*baud) - 2. The MiSTer core can put an emulated Mouse Systems mouse on channel A's RxD (OSD "Serial mouse: U74 A", not with the HR card) |
| CIO U66 | 0x0080-0x00FF, PCLK 750 kHz | IEEE-488; PB0 speaker (CT2 square wave); PB1 printer ACK; PC3 printer strobe |
| System latch | 0x0203 single-step; 0x0205 write: bit 0 IEEE REN, bit 1 keyboard enable | Write-only; `ksetlatch` keeps the 0x0205 copy |
| RTC MSM58321 | Bit-banged on U70 PB0-7 and PC1 | Shares port B with the printer |
| PDMAC + FDC + HDC | Doorbell: write 1 to 0x0500 | Command block at phys 0x080000 (hard disk) and 0x080010 (floppy); vector 0x80. Drives 0-1 are hard disks, 2-3 floppies |
| LR card | CRTC 0x0410 index, 0x0412 data (reads at 0x0413); control 0x0418 (0x28 video and blink on, 0 off) | 80x25 text; text RAM phys 0x370000, 2048 cells mirrored: a cell is one word, attribute in the high (even) byte, character in the low byte; byte writes reach only the character RAM, whatever A0; MDA attributes (0x07 normal, 0x70 inverse, 0x01 underline, 0x08 intense, 0x80 blink); CPU accesses wait for a character slot; only R14-R17 read back; SCCs at 0x0300/0x0320/0x0380/0x03A0 |
| HR card | 0x0404: write = control (any even port of the block loads it, 0x0400 included); word read = X port with the M/C strap in bits 1:0 | Mono 1024x800 or 1024x1024, or 640x400 RGBI, set by jumper JP3; bitmap phys 0x3E0000; see [HR card](#hr-card) |
| HR mouse | 0x0400 X, 0x0402 Y, word reads | Buttons in bits 15:13 (left, middle, right; 1 = pressed), bits 12:10 read 0, 10-bit wrapping counters in bits 9:0, no interrupt: poll it |
| LC card (8568) | 0x0181 index/status, 0x0183 data, 0x01FF ID = 0xC1 | Byte cycles only; 64 KB VRAM reached only through registers; all registers reset to 0, so program every one |
| APU | Extended instructions | Z8070 floating-point unit on CN13 (MiSTer option, OSD FPU); see [APU](#apu) |
| LANCE Am7990 | 0x0700 RDP, 0x0702 RAP; address PROM 0x0710/0x0712/0x0714 | Non-stock card; word cycles only; see [LANCE card](#lance-card) |

## Quirks

- Wait for SCC All Sent (RR1 bit 0, bounded) before resetting or
  reprogramming the SCC. On a video machine the ROM leaves Tx disabled, so
  program the SCC before polling it: its SCC register test resets both
  U74 channels (WR9 0x82, then 0x42, leaving NV set) and walks WR2, WR12,
  WR13 and WR15 to 0, and only the serial console's first character
  reprograms the chip afterwards.
- In an SCC handler, issue Reset IUS (WR0 = 0x38) before returning.
- Keyboard: after every key, clear port A's IP/IUS, then strobe PC3 (write
  0x70 then 0x78, each ORed with PC0-PC2 as read, so PC1 keeps its level;
  COHERENT writes 0x70/0x78). Without the strobe no further key arrives.
  Clear IP before the strobe: a byte already queued behind it sets PA7
  again as soon as PC3 rises. See "CIO U70 sharing" and "Keyboard".
- A presence test must restore the word it wrote and use two complementary
  patterns. An ID-byte probe must first write a wrong value, because an empty
  bus can echo.
- The boot ROM strobes the Centronics port once before kboot runs (seen
  on the Go model as one 0x91 byte with the smoke image, which touches
  neither CIO U66 nor port B): an attached printer may print a stray
  character at power-up.
- kboot's `bi_console` is a decision for SER, LR and HR. LC means "an ID card
  answered; probe it yourself".
- 8568: the card holds /WAIT until the chip has taken each access, so the
  index and status ports never need polling. Before each R31 access or R30
  write, wait for Update Ready (status bit 7). The chip is busy, and raises
  INTR when it next becomes ready, after every R31 access, every R30 block
  and every R18/R19 write (which loads the data register). Status bits 4:2
  read 0, bits 1:0 the version. A block is R30 bytes (fill repeats the
  last R31 byte, copy reads from R32/R33 ascending) and leaves both
  addresses past its end. There is no character ROM: the font lives in
  VRAM at R28's base.
- 8568 R36 is the number of DRAM refresh cycles per line, 0 none
  (C900_MiSTer D-VID-9; cores before C900_e005332 did R36 + 1).
  `lc.display` writes 5, the C128's value, so the core now refreshes 5
  times a line rather than 6, one more slot a line for CPU accesses.
- 8568 R37 bits 7 and 6 set the HSYNC and VSYNC polarity at the pins,
  1 positive; they reset to 1, so R37 reads 0xFF (D-VID-10). The C128
  KERNAL's VDC table writes only R0-R36, so a C128 drives its 80-column
  monitor with the reset's positive pulses. `lc.display` writes 0xC0,
  as wb900's LC driver does, so the polarity does not change when the
  bitmap is lent or given back. The core accepts either polarity.

## LC interlace

`lc.display` uses it when the disk is built with `mkdisk.py
--lcmode=400` ([platform](platform.md#framebuffer)); the default stays
80x25 in 8x8 cells at 640x200. Checked on the core's 8568 RTL with a
VDC-only bench, and on the video path of the cores C900_1611d1e and
C900_e005332 (the current test core) on c900-c0's bench; not yet on a
board.

- **Registers.** 80x25 in 8x16 cells at 640x400 changes only R8 = 3
  (interlace sync and video), R9 = 0x0F, R23 = 0x10, and the cursor and
  underline lines: R10 = 0x0E and R11 = 0x0F (from line 14 to the last,
  as R10 = 6 and R11 = 7 in 8x8 cells), R29 = 0x0F. R4 = 0x20,
  R5 = 0, R6 = 0x19 and R7 = 0x1D stay: in this mode the row-line
  counter steps twice per scan line, so vertical registers still count
  rows of 8 lines per field. Fields are 264 and 263 lines (527 per
  frame, 15.7 kHz, 59.8 fields/s), 200 visible lines each; one field
  carries glyph rasters 1, 3, ... 15, the other 0, 2, ... 14.
- **VRAM.** Unchanged: text 0x0000 and attributes 0x1000 (2,000 bytes
  each). With R9 below 16 the chip reads 16-byte glyphs, so the 8x16
  font fits the 4 KB at 0x2000; 32-byte glyphs start at R9 = 16.
- **Core.** C900_1611d1e drives `VGA_F1` from the 8568's field, so the
  MiSTer scaler weaves the two fields: 640x400 8x16 text showed 0 of
  400 lines wrong on c900-c0's bench. Earlier cores tie it to 0 and
  show each field as a progressive frame (half the rasters, alternating
  at 30 Hz). C900_e005332 flags the lower field 1 and changes only the
  other R8 modes (C900_MiSTer D-VID-7, D-VID-8): in interlaced bitmap
  R12/R13 load once a frame and the second field continues in RAM where
  the first ended; R8 = 1 offsets the fields by half a line. Text in
  R8 = 3 is unchanged, so `lc.display`'s registers stay as above.
- **Cursor in the RTL.** The core's 8568 starts the cursor on the cell
  line equal to R10[4:0] and stops it on the line equal to R11, which is
  not drawn. The line counter runs 0 to R9, so an R11 of the cell height
  never matches and the cursor stays a full block; c900-c0's board
  showed this in both modes. Whether real silicon draws the R11 line
  has not been checked, so R11 stays within 0..R9: R11 = R9 (0x07 at
  640x200, 0x0F at 640x400; the C128 KERNAL's 0x07). The core then draws
  a one-line underline, line 6 or 14; a chip that draws the R11 line
  would show lines 6-7 or 14-15.

## HR card

- **Modes.** JP3 selects the mode; no register does, and there is no ID
  register. A word read of 0x0404 is the mouse X port (A1 = 0) with A2
  high, so bits 1:0 carry a constant instead of the quadrature phase:
  the M/C strap, 2 (binary 10) for mono and 1 (01) for colour (FN#7
  U756, `rtl/hr_display/hr_pal20x8.sv` lines 41-46; in the core the
  strap is an OSD option, `rtl/c900_core.sv` lines 412-416). Bits 15:13
  are the buttons and 9:2 the X count, as at 0x0400. The core and the Go
  model read 2 for 1024x1024 too (PLEB p.125 says 3), so kboot's
  `BF_HR1024` tells 800 from 1024 lines. The read decode and the PAL
  equations are inferred (the PALs are undumped), the strap value least
  certainly, so `hrstrap` in `devices/console.c` is the one table that
  maps it, and `mkdisk.py --hrmode` overrides it
  ([platform](platform.md#framebuffer)).

  | Mode | ID | Raster | Pixels |
  | --- | --- | --- | --- |
  | 1024x800 mono | 2 | 128 bytes | 1 bit, bit 15 of a word leftmost, 1 = lit |
  | 1024x1024 mono | 2 (3 in PLEB) | 128 bytes | as above |
  | 640x400 RGBI | 1 | 320 bytes | 4 per word, bits 15:12 leftmost; nibble I, R, G, B |

- **Bitmap.** Physical 0x3E0000-0x3FFFFF, linear across the 3E/3F
  boundary. A mono raster never crosses it (raster 512 starts at 3F0000);
  colour raster 204 does, at x = 512. Word and byte accesses work; every
  access waits for the card's CPU slot (1-5 wait states).
- **Control** (U312, a 74LS174 latching data bits 5:0 on the rise of
  -SEL W, `rtl/hr_display/hr_u312.sv`; cleared by reset, so the screen is
  black until it is written): bits 1:0 BKS1/BKS2 bank select, latched but
  connected to nothing (`hr_card.sv` line 105: one 128 KB bitmap, no
  banking or double buffering; write 11, as the ROM and COHERENT's 0xFF
  do), bit 2 MONO EN, bit 3 MONO I (1 = full, 0 = 80 %; `hr_card.sv`
  lines 116-117), bits 5:4 unused. Bits 2-3 act on the mono stage only.
  -SEL W is any I/O write with A1 = 0 in the card's 0x04xx block
  (`hr_u755.sv` line 32), so 0x0400, 0x0404, 0x0408 ... all load it.
  Byte writes work (the CPU drives the byte on both lanes). 0x03 blanks,
  0x07 dims, 0x0F is full (`FBHR_` in `<resources/framebuf.h>`).
- **Presence.** Only the bitmap answers memory cycles. With no card, 0x3F
  may be the ROM's stack RAM and the ID and mouse ports float.
- **Mouse.** Bits 9:2 of each axis are a PAL counter and bits 1:0 the
  current quadrature phase, so bits 9:0 step once per quadrature edge.
  Right and down count up. Reads do not clear the counters.

## APU

The Z8070 sits first on the VI daisy chain and talks to the CPU through
extended instructions (first byte 0E, 0F, 4E, 4F, 8E or 8F, EPU ID in
bits 1:0). Sources: the core's `z8070_epu`, `z8070_ip`, `z8070_dec`,
`docs/HANDOFF_FPU.md`, Zilog's Z8070 and Z8000 manuals.

- **FCW EPA** (bit 13) clear: every extended instruction takes the
  extended-instruction trap (PSA +8) after its first word; the CPU saves
  the address of the second word and pushes the first as the identifier.
  EPA set with no APU: the instruction runs and reads float to all ones.
- **Registers.** F0–F7, 80-bit extended. Control: SC (0, 2 words,
  system), FFLAGS (4, 2 words), USER (5, 1 word: rounding mode bits 1:0,
  trap enables 15:8), PC1/PC2 (1, 2), FOP1/FOP2 (6, 7, 5 words). Reset
  clears SC (but its ID bits), USER and FFLAGS: no interrupts, no
  overlap.
- **Templates** (word 2 = op[5:4] << 14, rounding 13:12, register 11:8,
  op[3:0] << 4, word count − 1): memory to EPU `mode 0F|reg<<4|04`, EPU
  to memory `...|0C`; CPU to EPU `8F08|F<<4`, EPU to CPU `8F00|F<<4`;
  internal `8E04|Fd<<4`. Ops: FLD 20, FADD 18, FSUB 19, FMUL 1B, FDIV 1A,
  FSQR 2C, FINT 2D, FLDIL 22, FLDTL 24, FLDCTL 30. Rounding 3 is double.
  The assembler has no EPU mnemonics: code carries the words.
- **Snooping.** The EPU takes a first word at status 1101 and the next
  1100 read as its second word, wherever it comes from: after an EPA
  trap that is the PSA FCW ([internals](internals.md#z8070-epu)).
- **Clocks.** CLK.D is 24 MHz at 6 MHz CPU. The core's EPU has a known
  fault above 6 MHz: qualify at 6 MHz only.

## CIO U70 sharing

CIO U70 carries the tick (CT3), the keyboard (port A, PC2, PC3), the RTC
(PB0-7, PC1) and the Centronics data lines (port B) and BUSY (PC0). Its
registers are addressed directly, so separate registers never interfere;
shared ones follow these rules:

- **MCCR**: read-modify-write with interrupts off. Never clear PCE/CT3E
  (bit 4, the tick and port C) or PBE (bit 7). Program a port's mode
  registers only while that port is disabled. Port B stays a bit port.
- **Port C data**: write with D7-D4 write-protecting every bit you are not
  changing, and also preserve those bits in D3-D0 (read-modify-write with
  interrupts off), since a plain-latch model ignores the protect bits. The
  keyboard's PC3 strobe is `0x70` then `0x78`, ORed with PC0-PC2 as read:
  `0x72`/`0x7A` while /CS is high, and PC1 stays low when the keyboard
  interrupt lands inside a battclock call. PC1 (/CS) is high except inside
  a battclock call.
- **Port C direction**: read-modify-write; PC1 and PC3 outputs, PC0 and
  PC2 inputs.
- **Port B data and direction**: written only at Task level under `Forbid`
  (never from an interrupt), so a battclock transaction is never
  interleaved. battclock restores both afterwards. Leave PB7 (STOP) low
  when idle: the emulator lets STOP halt the clock without /CS; the RTL
  gates it with /CS.
- **CT3 and port A registers** belong to the tick and the keyboard driver.
  timer.device also reads CT3's current count with VI off: a command/status
  write of RCC with the gate bit (`0x0C`) freezes it, and reading the LSB
  releases it. The C emulator does not model the current count (it reads 0).

## Keyboard

The keyboard's 6500/1 scans a 13x8 matrix plus five modifier inputs and
sends each change as one byte over a clocked serial cable: the PC XT set-1
make code, bit 7 set on release. The firmware auto-repeats a held key by
resending its make code. Caps Lock is a momentary key; on each release
(0xBA) the firmware toggles one of its port bits (0x0AD2), taken to be the
Caps Lock lamp. The CPU board's interface (311075:
LS299 and DAV flip-flop) presents one byte at a time:

| Signal | Meaning |
| --- | --- |
| PA6-PA0 | Code, 0x01-0x62 from the firmware's table |
| PA7 | DAV: a byte waits; the port A pattern match (vector 0x08) |
| PC2 | Set: the byte is a release |
| PC3 | Low clears the byte and DAV |
| Latch 0x0205 bit 1 | High releases the cable clock; while low the keyboard holds its bytes |

While DAV is set the interface holds the keyboard off, so nothing is lost
until the firmware's 11-byte queue fills. Codes 0x01-0x53 are the XT main
block and keypad, except 0x45/0x46 (F14, F15) and 0x53 (keypad 00); the
C900's own keys are 0x54 Help, 0x55 Del, 0x56-0x57 F12-F13, 0x58 keypad CE,
0x59 keypad `.`, 0x5A keypad `/`, 0x5B-0x5D W1-W3, 0x5E keypad Enter and
0x5F-0x62 the cursor keys (up, left, right, down). Ctrl (0x1D), the shifts
(0x2A, 0x36), Alt (0x38) and Caps Lock (0x3A) are the modifier inputs. The
MiSTer core's PS/2 adapter reaches only the XT codes a PC key has: the
arrows arrive as keypad 8/4/6/2, PS/2 keypad `.` as 0x53.

MSM58321 model notes: the D10 leap-selection field is a Gregorian offset in
the RTL (00 = leap when the year is divisible by 4) and a yearly countdown
in the emulator. They agree for a leap year written with 00, which is what
battclock writes; in the emulator a common year set by battclock gets a
29 February. Read decoding ignores the field.

## LANCE card

A non-stock card (the core's `am7990_lance_card.sv`, the Go model's
`chips/lanboard`): an Am7990 on CN11, modelled on the A2065.

- **Ports.** RDP 0x0700 and RAP 0x0702 (ADR = A1), word I/O cycles;
  byte cycles are not decoded. CSR1-3 are written only while CSR0 STOP is
  set. After reset CSR0 reads 0x0004 (STOP) and RAP reads back its 2
  bits.
- **Address PROM.** 0x0710, 0x0712, 0x0714: MAC bytes 0-5, two per
  word, big-endian (byte 0 in the high byte of 0x0710). The card default
  is 02:60:8C:0C:09:00. On the MiSTer, Main writes 00:80:10 and the low
  three bytes of the opened interface into it when it opens the
  interface ([host link](#lance-host-link)).
- **DMA.** The chip is a Z-Bus master downstream of the PDMAC. It drives
  a 24-bit physical address (A23:16 on the lines the 8716 uses) that
  bypasses the MMU, so it reaches every physical RAM page, the RAM card's
  included. Its pointers carry into A23:16, so a buffer may cross a
  512-byte boundary; it may not cross a physical page only because the
  mapping behind a logical segment can end there. CSR3 = 0x0007 (BSWP,
  ACON, BCON) for the Z8001: with BSWP, frame byte i is at buffer + i.
  Descriptors and the init block are plain words; descriptor rings are
  8-byte aligned.
- **Interrupt.** /INTR (INEA and any of MISS, RINT, TINT, IDON, BABL,
  MERR) is the card's /VI. It is a level with no IUS: the chip holds it
  until those CSR0 bits are written 1. The card is last on the VI daisy
  chain and answers IACK with its strap, 0x88.
- **Loopback.** MODE LOOP (external, through the card's transceiver)
  and LOOP | INTL (internal) receive the chip's own frames, at most 32
  bytes; the runt filter is off.
- **Model differences.** The Go model has no internal loopback, and its
  cable returns every frame the chip sends in any mode, so in normal mode
  a frame addressed to the station or broadcast comes back to it. On the
  RTL, and so on the board, a normal-mode frame is not received by its
  sender. The Go model omits MERR, BABL and RX/TX lookahead.

### LANCE host link

On the MiSTer the card's cable side (`rtl/host/lance_link.sv`) is a
frame ring in DDR3 at 0x30400000 shared with Main: the MacPPC7300's
"PPCE" layout with the magic 'C9EN', eight TX and eight RX slots of up to
1536 bytes. A patched Main (`MiSTer_c900net`, picked by `main=` in a
`[C900]` section of MiSTer.ini) polls it and bridges frames to the OSD's
"Network interface": None (no card), eth0, eth1, wlan0 or tap0. TX frames
lose their FCS on the way out; RX frames are padded to 60 bytes and get
an FCS. The transceiver echoes TX to RX half a bit later (carrier sense
and external loopback) and supplies the heartbeat. Main sets no IP: with
tap0 the host routes or bridges tap0 itself.

## What each test target provides

| Target | Has | Lacks |
| --- | --- | --- |
| Emulator (`bin/c900`) | CPU, MMU, RAM ranges (`--ram=LO-HI`), SCC receive interrupts, CIO tick, RTC, disk and floppy | Video registers, keyboard, mouse, LANCE, APU (every extended instruction traps, EPA set or not, with the PC past both words), printer and speaker (CIO U66 is a plain register file; U70 PC0 reads back its latch), SCC transmit interrupts |
| Go model (`simrun`) | The board, LR and HR cards, keyboard, mouse; with `-lan` the LANCE card and a frame-buffered cable that returns every frame (`-lanpeer` adds a host) | LC card; LANCE internal loopback, MERR, BABL; APU (the probe finds none; EPA clear traps) |
| MiSTer RTL (Verilator) | Everything above; the LANCE with `+lance`, a TFTP host with `+netpeer`, and the APU with `+fpu` | Fast turnaround |
