# Exec900 contributor instructions

These rules cover `c900/`, the Commodore 900 (Zilog Z8001) port of Exec816.
The [root instructions](../AGENTS.md) still apply wherever they are not
65816- or AltirraOS-specific; this page states the C900 counterparts.

## Fork rules

- This directory is a fork layer over upstream Exec816
  (`github.com/mkur/exec816`). All C900 work lives under `c900/`. Never edit
  a file outside it, so upstream merges never conflict. If an upstream
  change is needed, propose it upstream.
- Generate shared constants and record field order from upstream
  `abi/*.json` with `tools/genabi.py`. Do not hand-edit generated headers.
  A new upstream field type stops the generator; map it deliberately.
- Kernel policy is a K&R C port of the upstream Action! and assembly sources
  listed in [`UPSTREAM.json`](UPSTREAM.json) and the
  [kernel source map](docs/internals.md#kernel-source-map). Port upstream
  policy changes from those sources, test, then record the review with
  `python3 c900/tools/drift.py --mark UNIT`. Mark only what was actually
  brought in line.
- Keep one current C900 implementation per unit. A new port unit gets an
  entry in `UPSTREAM.json` and a row in the source map; an upstream area
  newly ported moves from `not_ported` to `units`.
- Classic Amiga Exec is the default model, as in the root instructions.
  Record every C900 departure from upstream or from the Amiga model, with
  its reason, in [internals.md](docs/internals.md) (design) or
  [platform.md](docs/platform.md) (contract). State unsupported behavior
  explicitly.

## Toolchain pitfalls

- Mark Williams C is K&R only: no prototypes, no `//` comments.
- `int` is 16 bits; pointers are 32-bit segmented. Call public API through
  the casting macros `tools/genlvo.py` generates from `fd/*.fd`; add or
  change a library call in its .fd file, not in a header. Kernel code
  calling an `x`-prefixed function directly must pass correctly sized
  arguments (`0L`/`NULL` for pointers, `(ULONG)` for masks and sizes).
- External names keep 15 significant characters; longer names are
  truncated silently and can merge. Run
  `python3 c900/tools/symcheck.py c900/kernel c900/platform c900/include`
  after adding external names. Code that needs longer externals renames
  them with a header `tools/shortnames.py` generates.
- Pointer arithmetic changes only the offset word. Objects never cross a
  64 KB segment, except `AllocMem` blocks over 64 KB, which code steps
  through with `PtrStep`; compute segment-relative ends as offsets, as
  `mh_Upper` does.
- Every compiled frame address is relocated against the stack segment, so
  every stack must come from `MEMF_STACK`.

## Stack-segment budget

Segment 0x3F is the C900 counterpart of 65816 bank zero: every Task stack
and every trap frame lives there. The
[stack-segment budget](docs/platform.md#stack-segment-budget) is
authoritative. For implementation slices, report the change in reserved
stack-segment bytes (zero included), counting headroom, alignment and unused
capacity, with per-Task costs separate from fixed reservations. Justify any
new or enlarged fixed reservation.

## Testing

Follow the root [two-tier testing policy](../docs/contributing/testing.md).

- Development checks (default): the build plus focused emulator tests for
  the changed behavior, `make -C c900 test T=<name>`; `make -C c900 check`
  when a change touches shared kernel paths. Report the tests actually run.
- Documentation-only edits need content and link checks.
- MiSTer and hardware qualification are a separate tier, run for releases
  or on request. Emulator runs never qualify the system on hardware; do not
  claim qualification from them.
- Tests are `tests/<name>.c` and must end deterministically (pass, fail or
  panic) so runs stay bounded.

## Merging upstream

```sh
git fetch upstream && git merge upstream/main
python3 c900/tools/drift.py          # units with unreviewed upstream commits
# port each listed change into c900/, regenerate the ABI, then
make -C c900 check
python3 c900/tools/drift.py --mark UNIT [--to upstream/main]
```

`drift.py --check` exits nonzero while any ported unit is stale. A tracked
path reported missing means upstream renamed or deleted it; update
`UPSTREAM.json` in the same change. Not-ported areas are reported for
information only.
