# Task crash requester (scope)

Status: implemented (2026-11-15) as decided below; the contract is
[Task crashes](../platform.md#task-crashes) and the design notes are in
[internals](../internals.md). This page keeps the scope that led to it.
It sized the AmigaOS 2.0+ crash requester ("Software Failure",
Suspend/Reboot) for a Task whose trap has no handler of its own.

## Today

A trap calls the Task's `tc_TrapCode` ([traps](../platform.md#traps)):
segment trap (2), privileged instruction (8), extended instruction with
no EPU (11) and `sc n` (32 + n). `AddTask` copies a NULL `tc_TrapCode`
from `SysBase->TaskTrapCode`, the kernel's `ktrapdef`, which prints
`exec900: trap N pc SEG:OFF fcw F` and raises `AT_DeadEnd | N`: the
system stops. NMI and a trap with no current Task go to `ktrapdef`
directly. A recoverable alert already reaches intuition through
dos.library's `Alert` patch ([alerts](../platform.md#alerts)); a
dead-end alert never does.

## Amiga behaviour

Sources: AROS `rom/exec/alert.c` and `useralert.c` (read for this note)
and the Kickstart 1.3 and 3.1 requesters as users report them. No
AmigaOS exec, dos or intuition source was available: the texts below are
the reported ones, and which Kickstart component builds the 2.0+
requester is unconfirmed. AROS builds it in exec.

- **Kickstart 1.x.** A crashed Task shows `Software error - task held`,
  `Finish ALL disk activity`, `Select CANCEL to reset/debug`, with one
  button, Cancel, which raises the dead-end guru and reboots.
- **Kickstart 2.0+.** Exec's default trap code raises the dead-end CPU
  alert (`0x8000000N`, N the 68000 vector). From a Task, with
  intuition.library on the library list, an EasyRequest-style
  system requester is shown instead of the guru: title `Software Failure`, body
  `Program failed (error #8000000N).` / `Wait for disk activity to
  finish.`, buttons **Suspend** and **Reboot**. AROS builds it in exec
  (`Exec_UserAlert`) with intuition's `BuildEasyRequestArgs`,
  `SysReqHandler` and `FreeSysRequest`; AROS adds More/Log and Power off.
- **Suspend.** `Alert` never returns: the Task calls `Wait(0)` and stays
  in `TS_WAIT` for good. Its memory, stack, open files, locks and held
  semaphores are kept; nothing frees them. Other Tasks keep running,
  so the user can save work, but anything that waits on the held Task or
  its semaphores waits too.
- **Reboot.** `ColdReboot`. Exec900 has none: the counterpart is the
  dead-end stop exec's `Alert` already does.
- **Where it does not apply.** From supervisor state (an interrupt, a
  trap in exec), or with no intuition, or when the requester cannot be
  built, the dead-end guru is shown and the machine resets, as before.

## Exec900 design

All of it lives in dos.library, as the recoverable-alert display does,
so the kernel is unchanged.

1. **Default handler.** dos.library's init sets `SysBase->TaskTrapCode`
   (public in `struct ExecBase`) to its `dtrap`, as Amiga programs set
   it. Tasks added from then on get it; earlier ones (boot Tasks) keep
   `ktrapdef`.
2. **Trap time.** `dtrap(n, fp)` runs on the trapping stack with VI and
   NVI off and must not `Wait`. It prints the kernel's trap line, then
   holds the Task only when all of these hold: `n` is not `TRAP_NMI`;
   the frame lies within `tc_SPLower`..`tc_SPUpper` (not the interrupt
   stack, so not an interrupt handler or `kswitch`); `tc_TDNestCnt` and
   `tc_IDNestCnt` are negative; and the stack has room below the frame
   for the requester (see costs). Otherwise it calls `ktrapdef`'s path:
   `Alert(AT_DeadEnd | n)`.
3. **Leaving trap state.** To hold, `dtrap` stores `n` and the faulting
   PC in the frame's r0–r3, sets the frame's PC to `dheld`, an assembly
   stub in `dosasm.s`, and the FCW to the Task's normal one (interrupts
   on), then returns: `IRET` resumes the Task at `dheld`, which pushes
   the two longs and calls C `dcrash(n, pc)`.
4. **Held.** `dcrash` runs as the Task. With intuition.library on the
   library list it calls `EasyRequestArgs` (−588) on the default public
   screen: title `Software Failure`, body `Program failed (error
   #%08lx).` / `Wait for disk activity to finish.`, gadgets
   `Suspend|Reboot`, taking its turn on the alert semaphore. Without
   `EasyRequestArgs` it falls back to `DisplayAlert`, left button
   Suspend and right button Reboot. Suspend prints `exec900: task NAME
   held` and calls `Wait(0)`. Reboot calls `Alert(AT_DeadEnd | n)`.
5. **No intuition.** The console line and the dead end, as today and
   as Kickstart without intuition. Holding with a console line instead is
   an open question.

## Costs

- **Kernel text: 0 bytes**, staying 53,244, 4 below the 53,248 page
  boundary. Every piece above is dos.library's. A kernel-side handler
  would cross the boundary: about 150 bytes of redirect code costs a
  whole 1 KB heap page.
- **Stack segment: no fixed reservation.** The requester runs on the
  crashed Task's stack: `EasyRequestArgs` and its window open needs
  several hundred bytes, which a 1 KB or 2 KB Task stack may not have,
  and a segment trap from a stack overflow leaves none. `dtrap` needs
  a headroom threshold below the frame, or a dedicated helper process
  (one stack, about 2 KB, in the pool) that shows the requester for the
  held Task, which itself waits on a signal.
- **dos.library:** an estimated 500–700 bytes of code (handler, stub,
  requester and fallback) and about 120 bytes of strings and
  `EasyStruct`; a helper process adds its stack and about 200 bytes of
  code.
- **A held Task** keeps all its memory and its stack, as on the Amiga.

## Decisions

- **Helper process.** The requester never runs on the crashed Task's
  stack. A crash process (2 KB) shows crashes in turn; a disposable
  requester process (2 KB) calls `BuildEasyRequestArgs` and
  `SysReqHandler`, so the crash process can abandon it after 2 seconds
  (timer.device) and fall back to `DisplayAlert`. The held Task runs
  `dcrash` from the top of its stack when less than 768 bytes are left
  below the frame.
- **No intuition.** The Task is held with a console line; the system
  keeps running.
- **Early Tasks.** `AddTask` copies `TaskTrapCode` at creation, so the
  root Task and Tasks added before dos.library's init keep the kernel's
  dead end.
- **Reboot.** The dead-end alert, as today.
- **`RemTask` of a held Task.** Unsupported.
- **input.device.** Its crash goes to `DisplayAlert` directly.
- **Costs as built.** Kernel text 0 bytes (53,244). dos.library: 3,220
  bytes of code, 450 of data and 50 of BSS, more than estimated above:
  two processes, the timeout and the fallback text.
- **Privileged instruction.** Only normal mode traps one, and every
  segment is system-only, so it arrives as a segment trap at the PSA
  entry with interrupts off: the dead end.

## Open questions (as scoped)

- Requester on the Task's own stack (cheap, fails on overflow) or in a
  helper process (always has stack, costs ~2 KB of pool and a handoff)?
- Without intuition: dead end (Amiga) or hold the Task and print a
  console line (more useful on a console-only system)?
- Should dos.library also replace `ktrapdef` in Tasks created before its
  init, or only Processes, as Kickstart 1.x AmigaDOS did for its own?
- Reboot: stop as today, or a real reset through kboot's warm-start
  entry if one is added to the platform contract?
- A held Task's semaphores and I/O stay owned; no reaper exists. Is a
  later `RemTask` of a held Task (from a shell command) in scope?
- Does Exec900's intuition (when ported) support `EasyRequestArgs` from
  an arbitrary Task, and does it depend on any Task (input.device,
  ramlib, a handler) that could itself be the one held?
