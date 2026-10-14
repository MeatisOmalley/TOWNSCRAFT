# Accepted leaf trimming + Claude hot-path publication

Published 2026-10-13 from merge `3ae2e2f7` (Claude tip `fa94f400`, accepted static leaf metadata `ddfb2d4a`).

## Shared player media

- ISO: `ISO/DOSCRAFT.ISO`; SHA-256 `f2716c9af440d879aa96a09b5d0293d677edd818a1bc420880691aa07851301f`.
- EXE: `ebf3299b9cebac25736b8f79fe615f5e947a69fa4b7de2d87184bc1a4de5c649`.
- Player: `runtime/game-play-8mpoas1d`; previous VM/disk and ISO retained.
- June 1990 custom emulator: DX/40, 8 MiB, ET4000AX, AHA-1542A SCSI/ST4385N.
- Prebaked 1024 world migrated offline: both save banks and active edited columns checked; player, inventory, chests, mobs, logical terrain and edit references preserved. Leaf exclusion uses existing voxel metadata and the existing shared height/material cache.
- Twelve exact hot-path defaults merged. Visual plant-billboard and scan-quad VGA prototypes remain disabled. No UI changes.

## Validation

Exact combined EXE: automated new/reload/V5 save smoke PASS (`runtime/game-smoke-1twwmzqt`) at 8 MiB. Primary `.cmd` launcher validation PASS. Fixed-leaf, distant-world/render, overlay, codegen and benchmark tests pass; four GCC i386 host checks skip on Windows, with Claude's Linux parity evidence retained. No 1992 VM test. Completed diagnostic processes closed.

Frozen graphics replay, one prebaked world and continuous 256-frame diagonal horizon walk per distance; other scenes use 32 poses. Main graphics Low, view distance 8. These are guest PIT graphics timings, not full-simulation FPS. Every scene hash matches before/after (18/18).

| Distant setting | Standing before / after (ms) | Walking before / after (ms) |
| --- | ---: | ---: |
| Low | 27.440 / 25.701 | 28.724 / 26.479 |
| Medium | 35.223 / 33.486 | 40.279 / 38.205 |
| High | 46.697 / 44.961 | 60.424 / 58.154 |

Low edit-call mean: 11.886 -> 10.691 ms. Combined EXE text/data/BSS: 364568 / 20992 / 1054208 bytes; the merge adds 4096 bytes text and 1024 bytes BSS.

## Private Low styling measurement (not shipped)

Copied only `far_render.inc` and `far_march.inc` into a private build, changed detail gates from >=2 to >=1, and redirected that build's renderer include. Low retains its 24-block band, 64-block panorama and original strip width. Grass tops/dirt sides, material patterns and slope/side shading become active. No second map or other data allocation. Production source and shared ISO retain original Low styling.

Compare the matched first pass only: same initial fixture/cache context, same merged optimizations, same route. Trial repeats Low in later passes whereas control sweeps settings, so later trial passes are not paired comparisons.

| Scene | Published Low | Styled Low | Added (ms) |
| --- | ---: | ---: | ---: |
| open_static | 25.701 | 26.733 | +1.032 |
| walk | 26.479 | 28.552 | +2.073 |

Walking far-stage increase: heightmap +0.753 ms, panorama draw +0.776 ms, panorama rebuild +0.006 ms. Remaining whole-frame difference includes changed VGA upload traffic. Styling is substantially cheaper than selecting Medium (38.205 ms walking). One matched pass is a cost estimate, not universal cost or visual acceptance.

Full guest reports and publication hashes: [merged-leaves-publication.json](merged-leaves-publication.json). Claude's separate Linux measurements: [PERFORMANCE.md](../PERFORMANCE.md).
