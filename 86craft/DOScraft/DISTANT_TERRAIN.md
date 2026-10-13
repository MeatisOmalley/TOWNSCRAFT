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
Normal sky/clouds draw first, then the panorama, then the heightmap band, then
the unchanged polygon world. The band marches front to back with a per-column
screen-height limit and one shared reciprocal per radial depth. Near samples
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
metadata is about 36 KB. `TERRAIN.TMP` is now 178,815,488 bytes. Base/edit offsets,
seed identity and V5 saves are unchanged. Block edits update the appropriate
two-by-two summary. On load, summaries are reconstructed from committed edited
columns once; pristine columns use the bake. Startup/load can therefore do
extra sequential map and edited-column reads. It remains within the existing
June 1990 8 MB arena sizes, without paging or more resident world columns.

## Measured cost

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
