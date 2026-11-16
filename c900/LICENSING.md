# Exec900 licensing

Exec900 follows Exec816's licensing ([../LICENSING.md](../LICENSING.md)),
including its application permission: independent programs, libraries,
devices and handlers may use Exec900's documented public interfaces under
licences of their authors' choice, whether loaded separately or put on the
same boot image. Exec900 implementation code is GPL-3.0-only
([../LICENSE](../LICENSE)).

## MIT interface files

The files below are what a separately built program, library or module
compiles against or links into itself. They are licensed under the MIT
License ([../LICENSE-MIT](../LICENSE-MIT)), so building against them does
not put that program under the GPL. Paths are relative to `c900/`.

| Files | Scope |
| --- | --- |
| `include/` | Public headers: Exec, DOS, devices, resources, utility |
| `fd/` | Library function descriptions (vector order and arguments) |
| Output of `tools/genlvo.py`: `include/lvo/*.h`, `--table` vector tables and implementation prototypes, assembly vector tables | Library call macros, LVO offsets, and a library's own jump table |
| `lib/` | Program startup (`startup.s`, `dstartup.s`, `dosstart.c`) and amiga.lib-style helpers (`amiga.c`) |
| `platform/progabs.s`, `platform/portio.s` | Absolute symbols and port I/O linked into programs and modules |
| `libc/`, Exec900-authored files | The C library's DOS layer and start-up |
| `tools/shortnames.py`, `tests/host/test_shortnames.py` | Host tool other repositories run on their sources ([long external names](docs/internals.md#long-external-names)) |

`tools/genlvo.py` itself is GPL-3.0-only; everything it generates is MIT,
so a separately licensed library can follow the disk-library recipe in
[platform.md](docs/platform.md).

## Third-party material

- The C library outside the Exec900-authored files in `libc/` is MWC
  COHERENT libc from `commodore-900-toolchain`, BSD-3-Clause. Programs
  that link it carry its notice, `libc/NOTICE`.
- `tools/cohfs.py` and `tools/sparse.py` come from `commodore-900-cpm`
  and keep its MIT licence.
- The console fonts' provenance and notices are recorded in
  `docs/internals.md` (HR display, LC display): the 8x16 font is MIT
  (u_vga16), the 8x8 font public domain (font8x8).

Each file carries an SPDX identifier, which is authoritative for that file.
