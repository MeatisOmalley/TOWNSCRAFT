# 18 — production build provenance

Production source: `9c11b1c1bf8d2c0542d93ded4597bf609846d734`.
Game integration commit: `39cf283e`.
Build date: 2026-10-08.

This was the latest integrated game code when published, with local terrain/save/input work,
master renderer optimizations, and master's fast layer mesher adapted to
cached columns. Player geometry no longer uses the ordinary four-step
background path. See `../../MERGE_EVALUATION.md` and
`../../PERFORMANCE_RESULTS.md` for the decisions and measured limitations.

Build from repository `fmtowns/townscraft`:

```powershell
python tools/perf_audit.py --name production-final --ref 9c11b1c1 --production --build-only
python tools/smoke_production.py build/perf-audit/production-final
```

Compiler: bundled Zig 0.13.0 Clang, x86-freestanding-none, i386, `-O2`,
integer-only/no FPU, freestanding, no PIC/PIE, no benchmark defines.
The source snapshot is untouched: no synthetic level, frozen physics,
test trajectory or instrumentation is present in this ISO.
Kernel: 185,420 bytes / 91 CD sectors.

ISO SHA-256:
`ce0185e3434761210a63e6c230d95a9289831c8f461e150f995efc16af8d1b8f`

At publication, `TOWNSCRAFT_18_layer_merged.ISO` and `../../TOWNSCRAFT.ISO` were
byte-identical. The root image now follows build 20's lighting/texture-option source;
this numbered ISO remains unchanged for comparison. Build 17
and its compatibility launcher intentionally remain historical; they are
not aliases for this build.

Double-click `run_18_layer_merged.cmd`. It resolves the emulator, ROM and ISO
relative to its own location, irrespective of the caller's working directory,
and now uses the shared late-1989 2H-class / 16 MHz / 8 MB / 80387 profile
with single-speed CD and a separate 200 MB SCSI image. See
`../HARDWARE_1989.md`. This changes launch hardware only, not the ISO above
or the original build/test provenance below. The launcher does not attach
or overwrite a save floppy.

Validation: all thirteen native regression configurations passed. A real
2 MB / 16 MHz emulator boot, using Return through the keyboard, reached
96-wide gameplay with advancing ticks/frames and nonempty meshes. The smoke
test's matching ELF is retained under ignored `build/perf-audit/production-final`.
