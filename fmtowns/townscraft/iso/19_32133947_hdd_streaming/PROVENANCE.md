# 19 - HDD terrain streaming

Production source: `32133947eadc33d437f962162deda5df606d37cf`.
Build date: 2026-10-09.

This adds temporary SCSI HDD terrain backing and an 8 MB RAM read cache to
the integrated game. It retains generation, logical world sizes, controls,
graphics/mesh/lighting algorithms and permanent floppy saves. The HDD archive
is runtime scratch, not a recoverable permanent save. See
`../../HDD_STREAMING.md` for the implementation, hardware audit and limits.

Build from repository `fmtowns/townscraft`:

```powershell
python tools/perf_audit.py --name hdd-32133947 --ref 32133947 --production --build-only
```

Compiler: bundled Zig 0.13.0 Clang, x86-freestanding-none, i386, `-O2`,
integer-only/no x87 instructions, freestanding, no PIC/PIE, no benchmark defines.
The production source snapshot is untouched: no synthetic level, forced poses,
frozen physics, test trajectory or test-only instrumentation is present.
Kernel: 193,780 bytes / 95 CD sectors.

ISO SHA-256:
`9cd413e57e18cbb2deda7d4a97a7d0374ae43c27a53cd1b6b8895f8876cafcb4`

At publication, `TOWNSCRAFT_19_hdd_streaming.ISO` and `../../TOWNSCRAFT.ISO`
were byte-identical. The root now follows build 20; this numbered image
remains the unchanged HDD-streaming checkpoint for comparison.
All older numbered ISOs, including #18, remain unchanged.

Double-click `run_19_hdd_streaming.cmd`. It uses the shared 2H-class
16 MHz / 8 MB / enabled 80387 / 1x CD / normal-I/O profile, plus its own
200 MB SCSI ID 0 scratch disk. The helper creates/claims only a missing or
completely blank disk; unknown data is refused. Historical ISOs do not gain
the new driver from mounting the same kind of disk. No floppy is auto-mounted.

Validation: all 17 native regression configurations pass. This byte-identical
production image reached advancing gameplay on 8 MB with HDD backing
(256 cooking writes, 25 starting-column reads, zero errors), and on 2 MB without
an HDD. A separate test-only emulator build verifies edit/evict/revisit,
torch lighting, and physical HDD bytes. The full six-phase standard benchmark
completed with zero storage errors; see `../../HDD_STREAMING.md`.
