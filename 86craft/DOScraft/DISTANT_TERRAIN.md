# Optional distant terrain (B8)

Settings → Graphics → Distant terrain cycles **Off / Low / Medium / High**.
It starts Off and is independent of the main graphics preset. Every main
preset now starts with an eight-block total polygon distance; manual textured
and flat distance controls still work. Distant terrain begins at the **total**
polygon distance, behind both textured and flat-shaded blocks.

| Distant setting | Live heightmap band | Cached panorama | Horizontal sample width |
| --- | --- | --- | --- |
| Low | Total polygon distance–24 blocks | 24–64 blocks | 8 display pixels |
| Medium | Total polygon distance–32 blocks | 32–96 blocks | 4 display pixels |
| High | Total polygon distance–40 blocks | 40–128 blocks | 2 display pixels |

These are approximate terrain distances, not entity/simulation distances.
Normal sky/clouds draw first, then the live heightmap band, then only the
exposed panorama above its skyline, then the unchanged polygon world. The band
marches front to back at constant view depth. Gain is folded into the shared
reciprocal table each frame; elevation needs one multiply. Each ray starts at
total polygon distance divided by its norm, meeting the radial polygon boundary. Near samples
use resident heights/materials; farther samples read a two-by-two surface map.
The panorama caches material/fog runs for 256 directions. Turning scrolls it;
movement rebuilds 4/8/12 directions per frame. The first enable or a level
change builds a complete panorama. Day/night shades cached materials without
rebaking them. No disk I/O or additional face collection runs in this renderer.

The landscape uses a shallow-pitch approximation. Beyond roughly 28 degrees
up/down, and underwater/underground, the far field is skipped. The near renderer
keeps unrestricted pitch and its original projection. A heightmap cannot show
overhangs, tunnels or bridge undersides; distant trees become solid canopy
shapes. No distant mobs, item models, textures or torchlight are added.

The checked seed appendix adds **524,288 bytes** to terrain and RAM. Renderer
metadata is about 60 KB, including a bounded stationary-camera strip cache. `TERRAIN.TMP` is now 178,815,488 bytes. Base/edit offsets,
seed identity and V5 saves are unchanged. Block edits update the appropriate
two-by-two summary. On load, summaries are reconstructed from committed edited
columns once; pristine columns use the bake. Startup/load can therefore do
extra sequential map and edited-column reads. It remains within the existing
June 1990 8 MB arena sizes, without paging or more resident world columns.

## Original checkpoint cost

June 1990 DX/40, ET4000AX, 8 MB, SCSI; main graphics Low, total distance eight.
All three distant levels ran in **one process/world**. These are frozen graphics
replays with PIT instrumentation, not full gameplay FPS or a mechanical HDD
latency guarantee. Low/Medium/High below refer to the distant setting.

| Frame cost added versus Off | Low distant | Medium distant | High distant |
| --- | ---: | ---: | ---: |
| Still | 6.67 ms | 14.60 ms | 33.78 ms |
| Walking | 6.32 ms | 13.26 ms | 26.65 ms |
| Turning in house | 6.70 ms | 15.15 ms | 32.92 ms |

| Walking component | Low distant | Medium distant | High distant |
| --- | ---: | ---: | ---: |
| Heightmap | 3.51 ms | 7.55 ms | 16.23 ms |
| Panorama draw | 2.42 ms | 4.78 ms | 9.24 ms |
| Panorama preparation/setup | 0.42 ms | 0.96 ms | 1.11 ms |

The old 2–3 ms estimate was too optimistic. The first implementation cost
29–43 ms extra on Low main graphics. Wider writes, fewer radial samples,
incremental coordinates, direct baked-map lookup and cached color tables
reduced it. Off remains effectively unchanged. Steep up/down replays add
essentially zero because the far field is skipped.

Native tests exercise the real map loader, edits/removal/models, committed save
reload, corrupt-map rejection, all distant levels, both resolutions, cave/water/
pitch guards and the actual planar/chunky strip routines. Release smoke drives
the menu using the guest AT controller, exercises all levels and main presets,
then checks VGA, cold streaming, lighting and V5 new/load persistence. No host
key injection is used. See [evidence](tests/benchmarks/2026-10-13/distant-terrain.json).

The publisher upgrades disks on a new private copy, retaining every base/edit
byte and root file. The 1992 profile receives the same appendix/executable with
its hardware and CMOS preserved; no 1992 benchmark is required for this change.

## October 13 fill, cache and terrain-detail revision

The checkpoint was committed and pushed before revision, with tag
`codex/b8-before-tweaks-2026-10-13` at `7771fc69`. Current settings, distances,
near polygons and persistent terrain format are unchanged.

The live march keeps one upward-running raster pointer per ray. Its fill is
in the march translation unit, with layout/width dispatch outside the row
loop. Boundary sampling is inline. Panorama row endpoints use a 129-entry per-frame lookup; ordered
runs stop at the live skyline. Adjacent strips sharing angle and skyline can
be filled together. Loading a column whose coarse surface is unchanged no
longer invalidates the panorama. Per-sample counters are benchmark-only.

An exact-pose cache records material/fog strips after a second identical
camera frame, then replays fills. Position, yaw, pitch, resolution, distance,
map changes, every block edit and resident ownership/readiness guard reuse.
Colours remain live for day/night. Separate compiled march functions keep
recording out of moving frames. The bounded static cache needs no heap arena
and no increase in resident world columns.

Medium/High add directional slope shading, stable world-cell colour variation
and a two-colour pattern. Pairs of logical rows share the pattern to retain
VGA row broadcast savings. Low retains plain colour. Three spare material
bits encode style in RAM after validating the raw disk checksum. Edited
summaries refresh neighbouring style cells. Water/glass remain plain. There
is no extra map allocation or renderer disk I/O. Constant view-depth sampling
can slightly change coarse silhouettes; distance and near coverage stay intact.

| Total frame mean, old → current | Low distant | Medium distant | High distant |
| --- | ---: | ---: | ---: |
| Still | 30.15 → 27.36 ms | 38.08 → 33.24 ms | 57.26 → 41.49 ms |
| Walking | 41.08 → 40.07 ms | 48.02 → 46.28 ms | 61.41 → 56.27 ms |
| Turning in house | 38.77 → 37.74 ms | 47.22 → 46.14 ms | 64.98 → 60.97 ms |
| Block-edit frames, excluding edit call | 38.11 → 36.86 ms | 46.69 → 44.72 ms | 65.16 → 59.53 ms |

Main graphics remains Low, eight blocks. Each sweep uses one generated world
and all distant levels. Totals include Medium/High detail and view-depth
sampling. Versus the earlier Off reference, current distant overhead is about
3.88/9.76/18.00 ms still and 5.31/11.52/21.51 ms walking. These are instrumented
frozen PIT graphics replays, not full simulation FPS.

| High distant component, old → current | Still | Walking |
| --- | ---: | ---: |
| Panorama preparation | 0.015 → 0.015 ms | 1.11 → 1.12 ms |
| Panorama drawing | 10.06 → 5.16 ms | 9.24 → 1.73 ms |
| Heightmap | 23.11 → 12.15 ms | 16.23 → 18.58 ms |

The detailed moving heightmap itself is more expensive; panorama savings
outweigh it overall. Before detail/view-depth/cache, the pixel-identical
radial revision measured 50.10 ms still and 52.32 ms walking on High. Its
panorama draw was 3.52/1.19 ms. Detail is a visible tradeoff, not free; ablation
stages remain in the evidence.

The optional `--far-skip-first` benchmark experiment is **not shipped**: actual
DOS captures show sky holes where near polygons fail to cover the first strip.
`--far-details` and `--far-view-depth` permit independent comparison. Merging
cached runs by final colour was deferred: materials/fog must stay available
for dynamic day/night shading; current merges use equivalent material/fog.

Native tests compare the unstyled radial optimization against the checkpoint
across 432 poses and cached repeats with identical pixels. Style/view-depth
variants exercise actual planar/chunky fills and render guards. Four focused
native tests and ten benchmark tests pass. The exact release passed automated
June 1990 new/load smoke, including guest AT menus, VGA, streaming, lighting
and V5 persistence. The shared ISO is published with an exact copy of the
previous player HDD. No 1992 emulator test was run.
See [revision evidence](tests/benchmarks/2026-10-13/distant-terrain-tweaks.json).

Forced fill inlining was tested separately and rejected: High still improved
41.49 to 41.05 ms, but walking regressed 56.27 to 57.26 ms (Low/Medium walking
also regressed). GCC keeps a local fill call in the published build. Removing
its call overhead alone does not justify the larger moving loop. The running
cursor, removed repeated clipping and row-loop layout dispatch remain.
