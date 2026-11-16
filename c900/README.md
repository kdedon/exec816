# Exec900

Exec900 is the Commodore 900 (Zilog Z8001) build of
[Exec816](../README.md): the same classic-Amiga-style Exec kernel (Tasks,
signals, message ports, memory lists) ported from Action! to K&R C. It is
loaded by kboot and runs as a freestanding system with a serial console,
mirrored on the video card kboot finds (LR, HR or LC).

**Status:** development. Tests run on the C900 emulator, a Go machine
model and the MiSTer core's RTL bench. Nothing is qualified on hardware.

## Prerequisites

Python 3 and the checkouts [DEPS](DEPS) pins. `make -C c900 deps` clones
them into `c900/deps/`; without it, sibling checkouts next to this
repository are used:

| Checkout | Override | Used for |
| --- | --- | --- |
| `../commodore-900-toolchain` (built) | `C900_TOOLCHAIN` | Mark Williams C compiler, assembler, linker |
| `../commodore-900-kboot` | `KBOOTSRC` | kboot source and `bootinfo.h` |
| `$KBOOTSRC/build/kboot` | `KBOOT` | the loader written to the boot disk |
| `../commodore-900-emulator` | `C900_EMU` | emulator binary and boot ROM |

Build the toolchain with `make all`, then `sh host/arz -b`; kboot with
`make`, which finds the toolchain beside it or in `C900_TOOLCHAIN`; the
emulator with `make`. Pass overrides on the command line, e.g.
`make -C c900 C900_TOOLCHAIN=/opt/c900-toolchain`.

## Build, run and test

```sh
make -C c900                  # build/c900/exec900.sys and exec900.bin
make -C c900 test T=tasks     # boot with tests/tasks.c as the root program, check PASS
make -C c900 check            # every test
make -C c900 test T=dosboot   # boot AmigaDOS: S:Startup-Sequence under the shell
$C900_EMU/bin/c900 --firmware=$C900_EMU/rom --disk=build/c900/exec900.bin
```

`exec900.bin` is a bootable hard-disk image: kboot on the boot partition,
the kernel and its root program, `hello`, on the exec partition. A test prints `PASS <name>` or
`FAIL <name>: <reason>` and stops the emulator. See the
[testing rules](AGENTS.md#testing).

## Layout

| Path | Contents |
| --- | --- |
| `include/` | Public C interface in the Amiga NDK layout; generated parts are written to `build/c900/include/exec/abi/` |
| `kernel/` | Kernel policy ported from `lib/exec/*.act` |
| `platform/` | Z8001 entry, trap stubs, console, tick and memory map |
| `devices/` | Device drivers and the LR, HR and LC display modules |
| `lib/` | Program startup code (plain and DOS) and link library |
| `libc/` | MWC C library layer over AmigaDOS |
| `math/` | IEEE double math libraries on the EPU or the C runtime |
| `dos/` | dos.library, the CON:, V7 filesystem and RAM: handlers, the shell and the `C:` commands (boot modules and disk files) |
| `fd/` | Amiga-style fd files: library vector order and C types |
| `tests/` | Emulator tests: hunk programs, one `main()` per file |
| `tools/` | ABI generator, disk image writer, upstream drift and symbol checks |
| `docs/` | [Documentation](docs/README.md) |
| `UPSTREAM.json` | Upstream paths each port unit tracks |

## Upstream

This directory is a fork layer over
[mkur/exec816](https://github.com/mkur/exec816) and edits no upstream file.
[AGENTS.md](AGENTS.md) describes the merge workflow and
`tools/drift.py`.

## Licensing

[LICENSING.md](LICENSING.md) has the details.

- The kernel, drivers, DOS, handlers, tests and C900 tools are
  GPL-3.0-only, as a derivative of Exec816 ([LICENSE](../LICENSE)), with
  Exec816's application permission for programs using public interfaces.
- The interface files a separately built program compiles against or
  links (public headers, fd files, generated call macros, startup and
  helper code, the C library's DOS layer) are MIT
  ([LICENSE-MIT](../LICENSE-MIT)).
- The C library is MWC COHERENT libc, BSD-3-Clause (`libc/NOTICE`).
- `tools/cohfs.py` and `tools/sparse.py` come from `commodore-900-cpm`
  and keep its MIT licence.

Each file carries an SPDX identifier.
