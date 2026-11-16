# Exec900 test targets

Every test image is a kboot boot disk (`make $(OUT)/<test>.bin`): the
shared `exec900.sys` with the test as the root program
([internals](internals.md#tests)). Three targets boot it and apply the
same contract: the console (SCC U74 channel B)
must hold `PASS <name>` (or the test's `EXPECT` text) and no line starting
`FAIL`.

| Target | Make | Models | Cost per test |
| --- | --- | --- | --- |
| C emulator | `test`, `check` | CPU, MMU, RAM, SCC, CIO tick, RTC, disks; the HR card's bitmap and mouse (`--hr`); the LC card's registers and VRAM (no picture) | under 1 s |
| Go machine model (`sim`) | `simtest`, `simcheck` | the whole board pin by pin, LR and HR cards, keyboard, HR mouse, RAM card | 2-4 min |
| MiSTer core bench (`rtl`) | `rtltest`, `rtlcheck` | the FPGA core in Verilator: LR, HR and LC cards, keyboard matrix | 2 h or more |

The emulator remains the default. Use `sim` for device drivers it lacks and
`rtl` for the LC card's video or questions only the RTL answers.

## Commands

```
make -C c900 simtest T=hello CARD=lr        # CARD: none (default), lr, hr
make -C c900 simcheck CARD=none BENCHJOBS=6 # smoke and every test, 6 at once
make -C c900 rtltest T=hello CARD=lc        # CARD: none, lr, hr, lc
make -C c900 rtlcheck CARD=none
make -C c900 benchhelp
```

`simtest` and `rtltest` accept `T=smoke`. Logs go to
`$(OUT)/bench/<test>.<target>-<card>.log`, the screen at the end of the run
to `<log>.screen.*`, the tool's stderr to `<log>.err` and, for `rtl`, the
bench's full output to `<log>.tb`.

Inputs (`make benchhelp` prints them):

- `C900SIM`, `Z8000SIM`: the Go model's modules (default `~/git/C900/sim`,
  `~/git/z8000/sim`). `tools/simrun` is built against them into
  `$(OUT)/tools` through a generated Go workspace.
- `C900_MISTER`: the core (default `~/git/C900_MiSTer`). `tools/rtl.mk`
  builds its `tb/core` bench into `$(OUT)/rtl` with the bench's own source
  lists and flags; the ROM image comes from its `make_boot_rom.sh`. The
  GHDL and VDC netlists the bench needs are generated in the core's
  `obj_dir` by its own rules.

## Test directives

`EMUFLAGS` are translated where the target models them; a test needing any
other option prints `SKIP` and counts as passed.

| Emulator option | sim | rtl |
| --- | --- | --- |
| `--ram=08-17` | default | default |
| `--ram=08-35` | 3 MB RAM card | `+ram=2` |
| `--rtc=YYYY-MM-DDTHH:MM:SS` | RTC start time | skip |
| `--input`, `--input-mark` (with `\^X` control bytes, [internals](internals.md#tests)) | console input, a byte whenever the receiver is idle | skip |
| `--rtc=none`, `--rtc-ips`, other `--ram` | skip | skip |
| `FLOPPY: pattern` | pattern floppy in drive 0 | `+fd0=` |
| `WIRE: ...` | skip | skip |
| `SEND: PORT HEX` (the emulator attaches `sendwire.py` to `--wirePORT`: after the guest's first byte it sends the bytes HEX spells; each further `/`-separated stage follows one more guest byte) | skip | skip |
| `PRINTER: text` (no emulator option; the emulator has no printer) | a printer on CN5 (`-printer=LOG.prn`) whose output must contain `text` | skip |
| `KEYS: TICK:CODE,...` (the emulator skips the test unless it has `EMUKEYS`) | `-key=` injection | skip |
| `EMUKEYS: T:HH,...` and `HWMARK: text` (the emulator's `--key` codes, T loop ticks after `--hw-mark` text prints) | ignored | ignored |
| `MOUSE: TICK:DX:DY[:LRM],...` (the emulator's `--hr` and one `--mouse=T:DX:DY:B` per event: T is TICK / 16 loop ticks, the same guest time, from the start or `HWMARK`; B is L 4, M 2, R 1) | `-mouse=` injection with `CARD=hr`, ignored otherwise | skip |
| `BOOTS: N` (several boots of one image copy; `FSCK: BASE...` checks the V7 filesystem at each base after each) | skip | skip |
| `LANCE: loop` (the emulator skips the test) | the LANCE card (`-lan`) | the LANCE card (`+lance`) |
| `LANCE: peer` (the emulator skips the test) | the card and a host at 10.0.9.1 (`-lan -lanpeer=10.0.9.1`, pcap in `<log>.pcap`) | skip |
| `LC: card` (the emulator's `--lc`; kboot's probe then makes it the console) | ignored (no LC card) | ignored: `CARD=lc` fits it |

An `--rtc` seed only makes runs repeatable: `rtcread`, `rtcwrite` and
`rtcleap` pass at any valid time the clock holds. `rtcread` fails when the
clock holds no valid time. `rtcwrite` and `rtcleap` put the clock back to
the time it held plus the run, set on a second boundary; an invalid time
becomes 1978-01-01 plus the run, and a 12-hour clock comes back in 24-hour
mode. `rtctick` needs a valid time and can set the clock back by up to a
second. `rtcrace` needs its seed.

## Go machine model

`tools/simrun` drives `c900/machine` directly, without the debugger UI or
HTTP server. A tick is half a period of the 12 MHz crystal (24 M ticks per
simulated second); the model runs 0.5-0.8 M ticks/s, 2-3% of real time.

- Stop: a write to I/O port 0x0FFE (`emustop`), exit 0. Exit 3: tick
  budget (`SIMBUDGET`, default 1.2 G). Exit 4: no console byte for
  `SIMIDLE` ticks (default 150 M, about 6 simulated seconds).
- The disk image is opened read-only behind a copy-on-write cache.
- `SIMSC` selects the model's functional sub-CPU bypasses: default `hdc`
  (the hard disk 8049); `fdc` bypasses the floppy 6508 but fails
  `nofloppy`; `kbd` bypasses the keyboard 6500/1; empty runs every
  firmware, about 15% slower.
- The BIOS memory test is cut to one pass per segment, as the model's
  `bios.skip_memtest` option does. kboot starts at about 47 M ticks with
  the `hdc` bypass and 102 M without; console output is paced at 9600
  baud. `hello` stops at 130 M ticks (147 M without the bypass).
- Screen: LR as 80x25 text (`.screen.txt`) and the rendered 720x350 frame
  (`.screen.png`); HR as a 1024x800 PBM (`.screen.pbm`) and a best-effort
  text reading against the boot ROM's 16x16 font (`.screen.txt`; text a
  driver paints in another font reads as `?`; `tools/hrtext.py` reads it
  in Exec900's HR font). `SIMARGS=-hrjp3=4PC` fits the card as 1024x1024
  and `-hrjp3=M/C` as 640x400 colour (default `open`, 1024x800); the PBM
  is meaningful only in mono.
- Keyboard: `SIMARGS=-key=TICK:CODE[,...]` injects XT scancodes (make
  code, break = code | 0x80) through the keyboard shifter into CIO port A.
- Mouse (HR only): `SIMARGS=-mouse=TICK:DX:DY[:LRM][,...]` or a test's
  `MOUSE:` directive moves the FN#7 quadrature counters and sets the
  buttons; every event sets all three buttons.
- Printer and speaker: `-printer=FILE` attaches a printer on CN5 that
  appends every byte to FILE; without it BUSY floats high, as with an empty
  connector. `-wav=FILE` captures the speaker drive as a WAV file.
- LANCE: `SIMARGS=-lan` fits the card (`-lanmac=` sets its PROM). Its
  cable returns every frame the chip sends, in any mode. `-lanpeer=IP`
  puts a host on it (`tools/simrun/lanpeer.go`, MAC 02:00:0a:00:09:01)
  that answers ARP and ICMP echo with checksums checked, and after its
  first echo reply pings the sender three times, each request only after
  a correct reply; `lanpeer:` lines on stderr trace it, and
  `-lanlog=FILE` writes the frames as pcap.
- Lacks: the LC card, the LANCE's internal loopback.

Run `simrun` by hand for more control: `-stamps` prints each console line
with its tick, and the final stderr line reports the PC where the run ended.

## MiSTer core bench

`tb_core` runs the whole core on its real clocks. It has no stop doorbell:
a run ends `RTLIDLE` Z8001 clocks (default 6 M, one second at 6 MHz) after
the last console byte, or at `RTLMAX` clocks (default 120 M). Idle counts
only after the first byte, so a run with no console output goes to
`RTLMAX`. The bench applies `TBPATCH` (default its `skip_memtest.patch`).
It runs 3,000-6,400 Z8001 clocks per second, roughly 1/1000 of real time.

The bench holds at most 21 MB of hard disk (`HDMAX`) and reads only that
much of a larger image; the core then finds no drive type of that size
and reports no drive, so the ROM's auto-boot never loads kboot. `rtltest`
therefore boots its own images, `$(OUT)/rtlimg/<test>.bin`, built by
`mkdisk.py --geom=612,4,17,306` (drive type 1, 20.8 MB); kboot then
starts at about 25 M clocks. `diskread`'s raw-geometry check expects the
type 3 drive and fails there. `+speed=3` does not shorten a run: the
bench's cost follows CPU clocks, not simulated time.

- The image is copied first: the bench writes back as `hps_io` does.
- The transcript is rebuilt from the bench's `SCC B` lines.
- Screen: LR as text (`.screen.txt`, from `+screen`); HR as a PBM
  (`.screen.pbm`, from `+hrvram`). `RTLARGS=+frame=F.pgm +frameat=N`
  captures one LR video frame. The LC card has no capture; its I/O cycles
  appear as `LCMON` lines and its `_NVI` changes as `LCNVI` lines in
  `<log>.tb`.
- Keyboard: one matrix key per run, `RTLARGS=+key=N +keyat=CLOCK
  +keyhold=CLOCKS`. No mouse input.
- `RTLARGS=+fpu` fits the Z8070 APU (non-stock); `tests/epu` then
  checks the EPU path. Keep it at 6 MHz: the core's EPU is not qualified
  faster.
- `RTLARGS=+speed=3` runs the CPU at 48 MHz (non-stock).
- The bench's `+forks` option can share one boot among several runs; the
  runners do not use it yet.

## Timing

`tests/perf.c` runs the kernel and driver hot paths one phase at a time
and marks each boundary with an I/O write to port `0x0F00 + 2N`, which
nothing decodes. `tools/perf.py` reports the phases:

```
make -C c900 test T=perf
python3 c900/tools/perf.py emu EMUDIR build/c900/perf.bin \
    build/c900/exec900.sys build/c900/obj/p_perf.r
simrun -hd build/c900/perf.bin -board hr -shortcut hdc -marks 2>perf.err
python3 c900/tools/perf.py sim perf.err
simrun ... -profile perf.prof -profrange 30:32 2>perf.err
python3 c900/tools/perf.py prof build/c900/exec900.sys perf.prof
```

`emu` counts emulator instructions between marks, less those spent in
vectored interrupts, which it reports apart (a breakpoint on `perfmark`,
`tvi_` and the stub's IRET). `perfmark` is in the loaded program: a first
run stops at the kernel's `krun` to read the program's entry, and
`p_perf.r`, the program's `ld -r` output, gives `perfmark`'s offset from
it. `sim` gives 6 MHz CPU clocks from simrun's
`-marks` lines. `prof` sums simrun's PC samples (one per 16 model ticks
between two marks) per function; statics are named from the C source.
Samples in the loaded program count as `(program)`.
Console phases run only with a display card (Go model `-board lr|hr`).

`tests/conperf.c` times console lines (`KPrintf` and `CMD_WRITE`, with
and without scrolling) on any card. It marks its phases on the same ports
and also prints `@@conperf N` straight to the SCC, so the emulator's
profiler splits on it: `C900_PROF=1 C900_PROF_MARK="@@conperf "` with
`--hr` (`C900_HRCONF=2` mono, `1` colour) or `--lc`.

`tests/fsbench.c` times the boot volume: a 65,536-byte file written in
1,000-byte pieces and closed, read back and deleted. Each phase is
bracketed by marks 1 to 4 and printed in 100 Hz ticks (`fsbench: write
N ticks`), so a board run reports its times on the console. The C
emulator runs about 1.5 M instructions per emulated second and has no
disk latency; the Go model without `SIMSC=hdc` runs the controller's
8049 and the drive's rotation.

## Graphics test boot disk

A separate repository (wb900, say) can build a disk that boots straight
into its own test program with the HR display and its libraries. Paths
below: `C900` is this directory, `EXEC` a private output directory.

1. **Exec900.** `make -C $C900 OUT=$EXEC all` builds `exec900.sys`,
   every boot module (`$EXEC/hunks/`: the three display modules,
   `gameport.dev`, `dos.library`, `v7fs-handler` ...), the generated
   headers (`$EXEC/include/lvo/framebuf_lib.h` among them) and the
   program objects in `$EXEC/obj`.
2. **Program.** Compile each source as the Makefile's `CCRULE` does,
   without `-DEXEC_KERNEL`, with `-I$C900/include -I$EXEC/include`
   (and `-I$C900/platform` for `outb`'s port constants if wanted). Link
   the root program as a test is linked and convert it:

   ```
   ld-z8001 -r -o prog.r $EXEC/obj/l_startup.o prog.o ... \
       $EXEC/obj/l_amiga.o $EXEC/obj/portio.o $EXEC/obj/progabs.o
   python3 $C900/tools/mkhunk.py --entry=start prog prog.r
   ```

   It runs as the root Task on the 4 KB boot stack (`--rootstack=KB`
   sets 1 to 16 KB; [root stack size](platform.md#root-stack-size)),
   after the boot modules, with no arguments; when it returns the system stops
   ([root program](platform.md#root-program)). Its name holds at most 14
   bytes.
3. **Libraries.** Build each as a hunk without `--entry`
   ([building a disk library](platform.md#building-a-disk-library)).
   Either load it at boot with `--module=FILE` (every romtag in it is
   initialised before the root program; a file name holds at most 14
   bytes, so `graphics.lib`; the node's name comes from the romtag), or
   put it in LIBS: with `--put=Libs/graphics.library=FILE`, which needs
   `--module=$EXEC/hunks/dos.library --module=$EXEC/hunks/v7fs-handler`
   so `OpenLibrary` loads it (stored as `Libs/graphics.libra`).
   `--mkdir=PATH` makes an empty directory (`--mkdir=Trashcan`), with
   the directories on the way, as `--put` makes them.
4. **Disk.**

   ```
   python3 $C900/tools/mkdisk.py --kboot=$KBOOT --root=prog \
       --file=$EXEC/hunks/hr.display --file=$EXEC/hunks/lr.display \
       --file=$EXEC/hunks/lc.display --file=$EXEC/hunks/gameport.dev \
       --module=... --put=Libs/...=... gfx.bin $EXEC/exec900.sys
   ```

   `--hrmode=800|1024|rgbi` names the HR card's JP3 jumper when the
   strap read is wrong, as it is for 1024x1024
   ([platform](platform.md#framebuffer)). `--lcmode=400` puts the LC
   console in 8x16 cells at 640x400 interlaced (default 200).

   The exec partition (the `System` volume) is 1024 blocks (512 KB)
   unless its files plus headroom, a quarter of them and at least 128
   blocks, need more; it then grows in 64-block steps and the disk tests'
   pattern and scratch regions move after it. `--exec=BLOCKS` sets the
   size (it fails if the files do not fit). The inode area grows past 80
   inodes only when the files need it. The kernel's tests use the 1024
   default: `wd.device` tests address the pattern at block 1160.

   `--lnames` splits it: the exec partition keeps the kernel, modules,
   root program and `--file` files as the standard V7 volume `Boot`
   (`DH0:`), and the `System` volume (`SYS:`, `DH1:`), with everything
   `--put` and `--mkdir` add, moves to slot 13 after it in the
   [long-name variant](platform.md#long-name-volumes): names up to 30
   bytes, a longer one an error. `--sys=BLOCKS` sizes it as `--exec`
   does; the pattern and scratch regions follow it. `--sysbuffers=N`
   sets `DH1:`'s cache blocks (default 16). For Workbench:

   ```
   python3 $C900/tools/mkdisk.py --kboot=$KBOOT --root=prog --lnames \
       --module=$EXEC/hunks/dos.library --module=$EXEC/hunks/v7fs-handler \
       --put=Prefs/Calculator.info=FILE --put=C/ClickToFront.info=FILE \
       ... wb.bin $EXEC/exec900.sys
   ```

   Without `--lnames` the image is as before, byte for byte.

   The display modules and `gameport.dev` are loaded for the card the
   kernel finds, not from `Modules`. Without `hr.display` the console
   stays on serial and FreeFB has nothing to give back to; without
   `gameport.dev` there is no gameport.device. For the serial mouse on
   LR, LC or no card, also list it: `--module=$EXEC/hunks/gameport.dev`. The program finds the
   bitmap with `OpenResource(FRAMEBUFNAME)`
   ([framebuffer](platform.md#framebuffer)).
5. **Go model.** `make -C $C900 OUT=$EXEC simrun`, then
   `$EXEC/tools/simrun -hd=gfx.bin -board=hr -shortcut=hdc
   -log=gfx.log -screen=gfx.pbm` (add `-hrjp3=M/C` for colour,
   `-mouse=...` for input). The transcript has the console; the PBM is
   the final 1024x800 mono screen. The C emulator fits the card with
   `--hr` (`--hr-bitmap=FILE` writes the bitmap, `--mouse=` moves the
   mouse).
6. **MiSTer.** Copy the image to the board as a test copy, mount it as
   Hard Disk 0 (S2) with no floppy (the ROM boots a floppy first), and
   read the console on the serial port at 9600 8N1. The core's OSD
   settings are `C900_v12.CFG` (the `v,12` of its `CONF_STR`; check it
   before writing): byte 0 is status bits 7:0, byte 1 bits 15:8, byte 2
   bits 23:16, then zeros. They are sampled at reset, so write the CFG
   before loading the core.

   | Option | Status bits | HR test value |
   | --- | --- | --- |
   | Display Card | 12:11 | 1 HR (0 LR, 2 none, 3 LC) |
   | HR Mode (JP3) | 3:2 | 0 1024x800; 1 1024x1024; 2 640x400 RGBI |
   | Motherboard RAM | 4 | 0 1 MB |
   | CPU Clock | 7:6 | 0 6 MHz (stock) |
   | Serial Port | 15:13 | 0 U74 B, the console |
   | RAM card | 22:20 | 0 none (1 adds 2 MB) |

   HR 1024x800, 6 MHz, 1 MB is `00 08 00`; colour is `08 08 00`. The
   card's strap reads mono for 1024x1024 too, so in that mode Exec900
   describes and draws 1024x800 unless the image is built with
   `mkdisk.py --hrmode=1024`.
   The board is shared: get a slot from its coordinator first.

## Known limits

- With an LR or HR card fitted the ROM leaves SCC transmit disabled
  ([hardware reference](hardware.md)). The kernel's polled console
  (`con_putc`) then waits for Tx empty forever, so every test stops after
  kboot's `launching kernel` until the kernel programs the SCC. `simtest`
  reports this as `no console output` after `SIMIDLE`.
- The LC card holds `_NVI` Low from power-up (`LCNVI` lines). The ROM's
  self-tests and auto-boot run unchanged with it; `kintinit` releases it
  before interrupts are enabled.
- LC console modes: `lcdisp` (640x200) and `lcdisp400`
  (`mkdisk.py --lcmode=400`) read registers and VRAM back on the
  emulator's LC model, which draws no picture. The picture was checked
  on the core's 8568 RTL alone (a VDC-only Verilator bench: lc.c's
  registers in lcinit's order, VRAM as lcinit leaves it, one frame or
  two fields woven): text, font rows, attributes and underline match
  line for line in both modes, and the cursor cell is a full block
  ([hardware](hardware.md#lc-interlace)). Not yet run on a board: boot
  `lcdisp400`'s image with Display Card 3 (LC), then a shell image built
  with `--lcmode=400`, and look at the woven 8x16 text and the cursor.
- On `sim` (no card, 34 tests and smoke): `floppy` fails
  (`WD_CHANGESTATE` with a medium), `diskqueue` stops printing after
  `battclock` with and without the `hdc` bypass, and `nofloppy` fails
  only with the `fdc` bypass.
- `lance.device` has run only on the Go model (`lanloop`, `lanping`),
  whose cable returns every frame and has no internal loopback. Not yet
  run: `make rtltest T=lanloop` (the core bench with `+lance`), and the
  board: the C900 core with the LANCE card, `MiSTer_c900net` as the
  core's Main, OSD Network interface eth0 or tap0, stock 6 MHz. There
  `lanloop` needs nothing else; `lanping` needs a host at 10.0.9.1 on
  that interface that answers ARP and pings 10.0.9.2 three times within
  30 s (for example `ping -c 3 10.0.9.2` once the guest's ARP request
  arrives).
