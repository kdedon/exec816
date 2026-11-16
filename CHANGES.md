# Changes

## v0.1.0 (2026-11-17)

First release of Exec900, the Commodore 900 (Z8001) build of Exec816.
It boots from kboot and needs toolchain v0.2.0, kboot v0.3.0 and
emulator v0.4 or later ([DEPS](DEPS)).

### Kernel

- Tasks, signals, message ports, V39 signal semaphores, memory lists with
  MEMF attributes, blocks over 64 KB, AllocEntry, traps and interrupt
  servers.
- Libraries and devices over LVO jump tables; DoIO, SendIO, CheckIO,
  WaitIO and AbortIO with Amiga semantics.
- V39 memory pools, including MEMF_SEM_PROTECTED.
- Stacks in segment 0x3F, sized at boot; C:StackUse.
- Hunk files and boot modules; programs over 64 KB split into hunks.
- Z8070 EPU detection with lazy context switching.
- Recoverable alerts and the Task crash requester.

### Devices

- timer.device (AmigaOS 3.x interface) and battclock.resource.
- serial, parallel and beep devices.
- wd.device for hard and floppy disks.
- textcon.device over the LR, HR and LC display modules.
- keyboard.device, gameport.device and framebuf.resource.
- lance.device, a SANA-II driver.

### DOS

- dos.library: packets, processes, LoadSeg, assigns, residents, ReadArgs,
  patterns, variables, buffered I/O and dates.
- CON:, v7fs-handler (V7 volumes, short and long names) and RAM:.
- The shell and the C: commands.
- DOSBoot, the root program: S:Startup-Sequence, then an interactive shell.
- ramlib: libraries and devices from LIBS: and DEVS:.

### Libraries

- MWC C library over AmigaDOS.
- mathieeedoubbas and mathieeedoubtrans, on the EPU or in software.

### Tools

- genlvo.py, mkhunk.py, mkdisk.py, symcheck.py and runtest.sh.

### Release assets

- `c900-exec900-v0.1.0.tar.gz`: `Boot/`, the boot volume (exec900.sys,
  the boot modules, `Modules` and the root program DOSBoot), and
  `System/`, the SYS: tree (C:, L:Shell-Seg, S:Startup-Sequence).
- `c900-exec900-headers-v0.1.0.tar.gz`: public and generated headers, fd
  files, startup code, amiga.c, progabs.s and portio.s (MIT).
- `c900-exec900-tools-v0.1.0.tar.gz`: the disk, hunk and test tools.

Each carries VERSION, `.provenance`, `.contents` and its licences; all
three unpack into one tree.
