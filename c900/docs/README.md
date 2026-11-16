# Exec900 documentation

[Exec900](../README.md) is the Commodore 900 port of Exec816.

- [Platform contract](platform.md): boot handoff, memory map, stack-segment
  budget, tick, console, traps, DOS, the C library and unsupported
  features.
- [Internals](internals.md): toolchain facts, machine model, scheduling
  departures from upstream, the DOS and C library designs and the source
  map.
- [Hardware reference](hardware.md): physical memory, the MMU, DMA,
  interrupts, device ports and quirks, LC interlace, and what each test
  target models.
- [Test targets](testing.md): the emulator, the Go machine model and the
  MiSTer core bench, with their make targets, devices and costs.
- Design drafts (proposals for review, not contracts):
  [sharing the HR display](design/hr-display-sharing.md) with
  graphics.library; [an Amiga OFS/FFS handler](design/ffs-handler.md)
  for secondary partitions and HDF images (scope); [the Task crash
  requester](design/task-crash-requester.md), Suspend/Reboot for a
  Task's unhandled trap (implemented: [Task
  crashes](platform.md#task-crashes)).
- [Contributor instructions](../AGENTS.md): fork rules, toolchain pitfalls,
  testing and the upstream merge workflow.

The Exec API contracts are upstream's
[reference pages](../../docs/reference/README.md); C900 departures are
recorded in the platform contract and internals.
