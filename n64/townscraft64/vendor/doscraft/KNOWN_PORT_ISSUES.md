# Inherited storage issues and separately approved DOS fixes

These are reproduced by `tests/save_file_test.c`, not fixed as part
of the original DOS transport checkpoint. The vendor snapshot and gameplay/cache bodies
remain unchanged. They must be considered during real mixed-source parity tests.

1. **Completed save read versus reused scratch destination.** A completed
   `save_column_read` returns success for the same offset/length/destination
   without recopying. The world uses the same `columnScratch` destination for
   terrain HDD reads and RAM terrain-cache hits. Reading saved A, preparing
   terrain B, then preparing A again can therefore validate B's overwritten
   bytes as A and report a stream error. This behavior exists in Towns `save.c`
   and the original DOS transport preserved it. The native reproduction overwrites the
   shared destination after completion; a real column-cache integration replay
   is still required. **Approved DOS fix:** completion now releases the
   destination. An identical new request recopies from the retained track or
   rereads the necessary tracks; only an in-progress request owns the buffer.
   Regression checks cover both single-track cached recopy and a multi-track
   destination overwritten between completed reads. The vendor stays unchanged.

2. **Old v4 bank can shadow a later v3 save.** Save version selection is retained:
   cached widths below 256 use two-bank v4; the width-256 baseline uses legacy
   single-medium v3. The original loader prefers a valid v4 bank before testing legacy track
   zero. If v4 bank 1 remains valid and a new compressed v3 save fits entirely
   inside bank 0, the old v4 snapshot wins, yielding `SAVE_WRONG_SIZE` for width
   256 despite a successful v3 write. The native reproduction uses precisely
   this sequence. **Approved DOS loader-only fix:** when bank 1 would win,
   probe track zero. A readable `SAVE_MAGIC` header with version 1-3 selects
   the existing legacy loader instead. It performs the original size, RLE and
   checksum validation and returns its original errors; recognized-but-bad
   legacy payloads do not silently fall back to an older v4 snapshot. An
   unreadable probe, bad magic or unknown version retains v4 recovery. Loading
   never writes, invalidates banks or converts data; saving version selection,
   source-bank protection and v4 generation comparison remain unchanged.

   **Explicit limitation:** v1-v3 have no generation number. There is no way to
   determine cross-format chronology from these bytes; recognized legacy track
   zero wins even beside a genuinely newer/unknown-age v4 bank 1. This rule fixes
   a successful v3 save being hidden; it does not make legacy writes transactional
   or promise recovery from an unreadable/partially overwritten legacy header.
   Native regressions cover different/same widths, distinct old/new state,
   v1/v2 omissions, invalid legacy payloads, probe failures, v4 generation wrap/
   ties/recovery, both live-source directions and zero writes during loading.

   The extended guest fixture passes all gates and the independent whole-file
   byte oracle with a private `_CRT0_FLAG_LOCK_MEMORY` startup experiment
   (`saveio-486dx25-anqubxwe`). The ordinary unlocked executable instead faults
   in the DJGPP startup interrupt wrapper before the diagnostic begins. This
   is recorded separately; the save fix does not include a runtime workaround.

The user approved these separate bugfix commits alongside the port. Both are DOS
fixes; the immutable Towns reference is unchanged. Neither authorizes a renderer/
world optimization. Real world-cache/mob integration parity remains pending.

## Separate DOS-runtime startup correction

The enlarged save fixture exposed a pre-main `__dpmi_int` / `pop ss` GPF
(`EIP=bf4a`, error `00a4`) on the pinned interpreter, with unchanged blank
save media. A fresh run of the previous verified executable still passes.
Moving the test's 12-KiB buffer to static storage did not resolve the fault.
The runtime wrapper's stack pointer is eight bytes below its expected value,
with the expected interrupt-frame write straddling a 4-KiB boundary.

The pinned CPU source separately pushes FLAGS, CS and EIP and does not restore
the successful partial ESP decrement when a later write faults. A private
six-byte layout-preserving observation reads `3262` just below the reported
stack pointer, consistent with retry FLAGS under that explanation. These are
source/diagnostic evidence, not a captured CR2/page-fault trace; the exact fault
and retry remain inferred. No emulator, ROM or third-party runtime is patched.

The normal IRQ adapter now defines DJGPP's documented
`_CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY` startup policy. This requests
resident initial image/CRT-stack memory before startup DOS calls and retains
non-moving image addresses. Normal builds pass every extended `SAVEIO.EXE`
gate and the independent whole-file oracle, plus the `SYSTEM.EXE` startup/IRQ,
heap, profiler, VGA, shutdown and reinstallation gates. CRT lock errors are
unchecked, so the explicit checked ISR-image lock/failure unwind remains
mandatory. Installed RAM, game heap budgets, CPU accuracy, no-paging policy and
rendering are unchanged. Full-game memory headroom/parity remains a later gate.
