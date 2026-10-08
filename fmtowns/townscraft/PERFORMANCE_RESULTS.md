# Integration and performance results — 2026-10-08

## Decision, minimum spec first

Keep the cached terrain/save architecture and local downward-floor culling.
Use master's masked, dirty-layer mesher and pool accounting within that
architecture. Prioritize ordinary player edits synchronously; retain bounded
preparation for new meshes and streaming seams. Preserve master's renderer
optimizations and local gameplay/input/save behavior. No control/UI redesign
or GBA/SNES work is included.

Production integration is `39cf283e`; the standardized tools and fixture are
`9c11b1c1`. The final ISO is an uninstrumented production build, not one of
the benchmark images. See its numbered ISO folder for source/hash provenance.

The old visible block-removal delay was real: local's cache path published
ordinary edited meshes only after four background updates (twelve for a
corner affecting three chunks). Master's layer meshing is compatible with
cached storage after adapting neighbor/slot ownership and scheduling.
In the controlled edit experiment, ordinary removal drops from 57.80 ms of
mesh work over four updates to 7.28 ms in one update; corner removal drops
from 173.72 ms over twelve updates to 9.44 ms in one. These are mesh CPU
times, not whole-frame visible latency. See `MERGE_EVALUATION.md`.

## Actual minimum-spec cached extent: before / after

MODEL2, 16 MHz, 2 MB, 160x100 rendering, view 8, width 96. Three runs each;
table entries are medians. Old = selective merge `159d95f2`; new = integrated
`39cf283e`. Same fixed terrain, trajectory and settings on both sides.

| Separate phase | Old FPS | New FPS | Change |
|---|---:|---:|---:|
| Look around | 24.36 | 24.58 | +0.9% |
| Walk | 25.50 | 25.67 | +0.6% |
| Walk looking fully down | 19.47 | 19.60 | +0.6% |
| Walk looking fully up | 37.39 | 37.35 | -0.1% |
| Break / replace blocks | 8.35 | 9.51 | +14.0% |
| Mixed movement / pitch / edits | 18.75 | 25.68 | +37.0% |

Treat the small walking/down differences as effectively neutral: run ranges
overlap. Up is also practically neutral, not a demonstrated improvement.
The edit/mixed gains are clear. The common-width-80 repeated comparison
independently improves break 8.64 -> 10.38 FPS and mixed 19.73 -> 31.18 FPS.

Frame-time tails matter too. At width 96, walk p95 falls 62 -> 56 ms;
down p95 falls 136 -> 88 ms; mixed p95 falls 178 -> 72 ms. However, break
p95 rises 140 -> 150 ms and mixed's worst frame rises 197 -> 206 ms.
These are not universally hitch-free results. Maximum background-update
cost while walking falls about 55 -> 46 ms, still too large to ignore.
p95 values are lower bounds from 2 ms histogram bins, not exact percentiles;
maximum frame times are exact, with the table using median run maxima.

## Did mesh streaming cause the landmark #12 / #13 difference?

Both branches already descend from mesh streaming. Master did contain it;
local added a second kind of streaming for block/light storage.

The original #13 build reserved 24,000 quads on 2 MB machines, shrinking its
map from 96 to 80; later work reduced that reservation to 16,000 and cached
the nearby search. Original generated-world spawn/scene and mesh coverage
also differed. Those old FPS numbers are not an isolated streaming test.

In the standardized common-width-80 test, three-run medians are:

| Phase | #12 `4edc6210` | #13 `1f6d3626` |
|---|---:|---:|
| Look | 16.50 | 16.50 |
| Walk | 19.42 | 19.01 |
| Down | 6.25 | 6.20 |
| Up | 29.09 | 29.09 |
| Break | 8.06 | 8.16 |
| Mixed | 15.66 | 15.73 |

There is a small walking cost (~2.1%), not the dramatic collapse observed
across the original different builds/worlds. Walking update peaks rise
from ~0.17 ms to ~38.6 ms when streamed chunks are built. Streaming avoids
building the whole world's mesh, but moves some work into travel. This
fixture does not establish equal coverage for every historical generated
world or dismiss the user's observed stutters. The 80-wide fixture fits
all terrain columns in cache; the 96-wide results above separately exercise
terrain-window changes.

## Which later optimizations helped?

Minimum-spec historical sequence below uses one run per intermediate ref;
key start/end refs are repeated separately. These are commit-level matched
workloads, not proof that every individual line in a commit helps.

| Source checkpoint | Look | Walk | Down | Up | Break | Mixed |
|---|---:|---:|---:|---:|---:|---:|
| Timing / stream budget `56f599ec` | 16.49 | 18.95 | 6.23 | 29.09 | 8.15 | 15.79 |
| Layer / masked mesher `dfccfbb5` | 16.46 | 19.07 | 6.25 | 29.06 | 9.13 | 17.31 |
| Near split / offscreen rejection `bc26e2ec` | 20.64 | 24.73 | 6.41 | 29.97 | 9.82 | 17.87 |
| Camera / cloud tables `1121d0a7` | 21.87 | 26.01 | 6.46 | 29.97 | 10.01 | 17.81 |
| Triple buffering `ea931665` | 24.60 | 29.12 | 6.58 | 41.91 | 10.80 | 23.72 |
| Original master `e86b3e6f` | 24.59 | 29.12 | 6.58 | 41.90 | 10.80 | 23.72 |
| Old selective merge `159d95f2` | 24.42 | 28.38 | 22.18 | 42.42 | 8.67 | 19.92 |
| Integrated `39cf283e` | 24.60 | 29.11 | 22.43 | 42.99 | 10.38 | 31.18 |

- Layer/masked meshing: retain. The isolated full-versus-partial edit
  experiment also establishes work reduction independently of renderer FPS.
- Offscreen/near rejection: retain; look/walk improve ~25%/~30% at minimum.
- Camera/cloud tables: retain; look/walk improve ~6%/~5% at minimum. Do not
  call the tiny mixed decrease a proven isolated regression from one run.
- Triple buffering: retain; look/walk improve ~12%, up ~40%, mixed ~33%.
- Offscreen mob-box culling: the empty baseline is deliberately neutral.
  In a separate twelve-frozen-pig test, two-repeat FPS rises 21.638 -> 21.966
  at minimum (+1.5%), and 10.207 -> 10.408 at high (+2.0%). Retain.
- Local downward-floor culling: retain; down improves about 6.6 -> 22 FPS
  in the matched minimum fixture; 960 real-raster pixel comparisons pass.
- Column cache: retain for storage/save behavior and RAM headroom, especially
  above 2 MB. It is not established as a standalone FPS optimization. It
  introduces decoding, relighting and slot-aware access costs; they remain.
- Four-layer background rebuilding: retain for heavy new/seam work, not
  ordinary edits. Merely slicing the old mesher delayed publication and
  did not substantially reduce total work. No blanket speed claim for it.
- Lighting: not independently improved. Controlled torch lighting grows
  ~159 -> 172 ms, while total lighting plus meshing falls ~477 -> 209 ms.

## Validation and reproducibility

### Higher settings, evaluated separately

MODEL2, 25 MHz, 4 MB, 320x200 rendering, view 12, common width 80.
Three repeats of old selective merge versus integrated build:

| Phase | Old FPS | New FPS |
|---|---:|---:|
| Look | 11.78 | 11.80 |
| Walk | 13.04 | 13.24 |
| Down | 13.67 | 13.72 |
| Up | 45.50 | 45.93 |
| Break | 5.48 | 5.83 |
| Mixed | 21.44 | 28.77 |

Mixed improves ~34%, with p95 272 -> 66 ms. Break improves ~6.5%, but
p95 slightly worsens 214 -> 216 ms. The higher-resolution break workload
remains slow; no averaging with minimum-spec results conceals that.
The separate historical high-profile sequence is archived as single runs,
not used to override the repeated before/after numbers here.

Repeats within one batch are not the entire uncertainty: an earlier run
of the identical integrated high-profile test ISO measured 5.54 rather
than 5.83 break FPS. The old build also shifted between batches. The
initial timer/display/emulator state has not been fully normalized, so
small high-profile gains (including breaking) need caution; the substantial
mixed gain appears in both batches. Do not interpret narrow within-batch
FPS ranges as proof of equally narrow cross-session variability.

### Correctness and production boot

All thirteen native regression configurations passed against the integrated
sources: linear and cached randomized partial/full mesh equivalence, both
RAM profiles, stream search, cache/seams/pool ownership, floppy/save failure
recovery, dormant mobs, caves, keyboard/physics, and real-raster downward
pixel equivalence. The retained outgoing-column seam invalidation and safe
two-stage pool compaction are correctness repairs required by this port.

Production boot is checked separately using real emulator keyboard input:
2 MB / 16 MHz, width 96, advancing gameplay ticks/frames and nonempty meshes.
It does not patch guest memory or count the title screen as gameplay.

The standardized level and individual phases are reusable now; commands,
settings and limitations are in `BENCHMARKING.md`. Tests freeze AI/physics,
move the camera along deterministic poses and call real terrain edits.
They measure render/terrain work, not AI, mining-hold time or disk seek FPS.
Results use emulated time; emulator numbers are not real-hardware guarantees.
Raw phase/edit results, repeats, terrain hashes and test ISO hashes are
tracked in `tests/results/*2026-10-08.json`. Historical intermediate refs are
single-run observations; min-width-80/96 key comparisons use three repeats,
and mob tests use two. Do not compare different widths/profiles directly.
