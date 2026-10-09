# 20 - Near texture quality option and deferred edit lighting fix

Production source: `7e7fa1bd1287d0fc950acccd8880344834c148f4`.
Build date: 2026-10-09.

Includes all integrated/HDD-streaming work from #19 plus:

- PF10 cycles Adaptive (unchanged default), Full per-cell near subdivision,
  Fast/no subdivision. Fast trades texture warping for fewer polygons.
- Deferred edit relighting refreshes faces sampling the edited source cell,
  fixing stale lighting that previously needed a second block/torch edit.
- Mode changes invalidate optional occlusion proofs/hidden-item flags.

The only help change is a PF10 hint within the existing panel. No other
controls, gameplay, save format, terrain generation, hardware profile or
86Box/GBA/SNES work is changed. See `../../OPTIMIZATION_AUDIT.md` for the
September 30–October 8 master retention audit and verification details.

Build from repository `fmtowns/townscraft`:

```powershell
python tools/perf_audit.py --name lighting-subdivision-final --ref 7e7fa1bd --production --build-only
```

Compiler: bundled Zig 0.13.0 Clang, x86-freestanding-none, i386, `-O2`,
integer-only/no x87 instructions, freestanding, no PIC/PIE, no benchmark defines.
Untouched committed production source: no synthetic level, fixed seed,
forced poses, frozen physics, test trajectory or benchmark instrumentation.
Kernel: 194,228 bytes / 95 CD sectors.

ISO SHA-256:
`bbdb4559cd6188a999621fbaa8700754f984327f2c387d675c966ed857fcbde4`

`TOWNSCRAFT_20_texture_quality_lighting.ISO` and `../../TOWNSCRAFT.ISO` are
byte-identical. All older numbered ISOs, including #18 and #19, are preserved.
The original root-ISO emulator command therefore also runs this fix.

Double-click `run_20_texture_quality_lighting.cmd`. It uses the shared
2H-class 16 MHz / 8 MB / enabled 80387 / 1x CD / normal-I/O profile and its
own 200 MB SCSI ID 0 scratch disk. Unknown disk data is refused; no floppy
is auto-mounted. The HDD image is ignored runtime scratch, not a tracked save.

Validation: all 29 native regression configurations pass, including deferred
torch addition/removal across column/layer seams at 2/4/8 MB and floor/distance/
general-occlusion pixel comparisons in all three quality modes. Real-emulator
production boots at 2 MB without HDD and 8 MB with HDD reach advancing
gameplay; real PF10 keyboard events verify the sequence 0→1→2→0 on both.
The 8 MB boot records 256 cooking writes, 25 initial reads and zero errors.
Three alternating default-mode 2 MB standard benchmark repeats show FPS
medians within 0.6% of the previous source, with overlapping run ranges.
The new options do not claim to fix the separate house-rendering bottleneck.
