# 386SX-25 baseline: Very Low near and distant terrain

Measured on October 14, 2026 with the DTK 386SX-25 profile, 4 MiB RAM,
Trident TVGA8900B, 16-bit ISA emulator fix, and the 40 MB IDE disk containing
the 33,730,560-byte V2 compact seed-4242 terrain. Main graphics were Very Low
(80x50/Potato), adaptive textured rendering to four blocks, with Very Low
distant terrain (`DISTANT_LEVEL=1`).

Command: `python tools/benchmark_386.py --label very-low-far-very-low-20261014`.
One unreported fixture warmup, then three repeats of six scenes, 32 measured
frames per scene: 576 measured frames total. Timings come from the guest PIT;
the replay freezes simulation, so these are graphics/world-update replay FPS,
not a guarantee of unrestricted gameplay FPS. Far-substage instrumentation is
enabled. All three guest reports passed and the six scene hashes match across
repeats. The completed private emulator was closed; its disk and raw reports
remain at `runtime/bench-386-24huig3j`.

| Scene | Mean frame time | FPS |
| --- | ---: | ---: |
| Open, stationary | 119.534 ms | 8.366 |
| Walking | 166.158 ms | 6.018 |
| Walking, looking down | 169.385 ms | 5.904 |
| Walking, looking up | 105.149 ms | 9.510 |
| House | 219.441 ms | 4.557 |
| Block edits: following rendered frame | 218.958 ms | 4.567 |
| All rendered frames | 166.437 ms | 6.008 |

Whole-repeat FPS: 6.008202, 6.008188, 6.008402. FPS is total frames divided
by total elapsed time, not an average of instantaneous FPS. Block-edit calls
add an average 75.224 ms; call plus following frame is 294.182 ms, or **3.399
edit cycles per second**. The short run is insufficient for robust tail-FPS
claims (only six samples in the 1% tail).

Average stage time across all scenes:

| Stage | Mean time |
| --- | ---: |
| Near drawing | 46.777 ms |
| Sky and distant terrain | 35.362 ms |
| World update | 34.113 ms |
| Geometry collection | 24.291 ms |
| Present/upload | 14.912 ms |
| Raster finish | 10.181 ms |

Distant-terrain substage times are nested in the sky/distant stage, not
additional frame costs. Walking spends 32.455 ms on the live distant heightmap;
the house spends 38.500 ms there. Looking down spends 90.576 ms on near drawing,
with no distant-terrain work. These identify near drawing, world/geometry
preparation and the distant heightmap as useful next optimization targets.

The production startup overlay now selects Very Low on detected 386 CPUs;
later CPUs retain Low startup. F2 still switches the allowed presets. The
seven focused graphics-policy/source-overlay tests pass, including compiled
386 startup and later-CPU reset behavior. The playable executable was rebuilt
and installed via `refresh_386_vm.py` into `runtime/game-386-_otljhs2`. Its HDD
and configured CMOS are exact copies of the previous player VM, which remains
intact. The distant-terrain preset default itself was not changed by this
startup patch; this benchmark explicitly fixes distant quality to Very Low.

[Raw samples, hashes, hardware manifest and source recipes](386sx25-very-low-far-very-low-2026-10-14.json).
