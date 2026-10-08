# Streaming

## Implemented first pass

Terrain now uses 25 reusable 16 x 16 x 48 column slots. Blocks and light
occupy 600 KiB together regardless of the logical map width. Each column
owns three mesh chunks; heights and mesh metadata follow its physical
slot. Global coordinates remain stable while slots change owners.
The existing 96/160/208/256 world extents, view presets and render
resolution are preserved. Interlacing remains removed.

The resident window follows position, with nearest-first loading and an
outer ring for lighting and preparation. Motion begins preparing the
entering strip four blocks before a column boundary. Turning in place
does not replace terrain. Loading, sky light, torch light and mesh work
are separate steps; light queues retain their channel between frames.
During travel, direct skylight initialization processes at most 64 vertical
stacks per frame. Sky flood seeding also processes at most 64 stacks,
skipping open sky above all neighboring obstructions and resolving each
stack's neighbor indices once. Torch seeding scans sources in the incoming
column and existing light at its border, without repeating a six-neighbor
search for every source. Propagation retains its 2,048-pop travel budget.
Light-dependent meshes wait until the lighting job settles, preventing
repeated rebuilds against intermediate light. New meshes and deferred
refreshes share the gameplay frame's background mesh budget.
Shared-face refreshes caused by incoming columns use the same one-mesh
per-frame deferred budget as light refreshes. Player block edits retain
immediate rebuilds. Incoming columns build their new meshes through the
existing nearest-first mesh budget rather than invalidating them again.
Animals activate only with prepared collision terrain. Missing terrain
is solid, and live bodies/fuses pin their required columns. Failed reads
or a full backing buffer retain the old slot and its edits.
Loaded explosives request their missing blast columns before their timers
resume; they cannot detonate against absent terrain.

Ordinary animals deactivate beyond 32 blocks and reactivate within 24.
Their entire gameplay state and stable identity are preserved. Dormant
animals receive zero ticks, with no elapsed-time replay on return.
Ordinary distant hostiles despawn. Primed TNT and live explosive fuses
remain active until their existing behavior resolves. The existing 5 Hz
fallback remains for distant active mobs that cannot retire safely; there
is no additional reduced-tick surrounding tier. The full simulation cap
remains 24, with reserved hostile and TNT slots.

Animal regions initialize once from a population seed. Their initialized
marker survives even when empty. Killing the population does not cause
the initial animals to regenerate. Natural hostile spawning remains
independent of these markers. Nearby saved animals get available slots
before new regional populations.

## Backing and saving

New worlds initially retain their finite terrain in a bounded compressed
RAM archive: 128 KiB on 2 MB, 384 KiB on 4 MB, and 1 MiB on larger profiles.
Generation still runs once for the existing finite map; this preserves
its terrain and seed behavior. Generation scratch is temporary and is
released afterward. This is not yet an on-demand larger-world generator.

For maps below 256 blocks wide, a successful save switches clean column
backing to the dedicated 1,232 KiB floppy. The compressed RAM archive then
contains only pending changed columns. Reloading retains only the nearby
uncompressed columns and the saved record directory; travelling reads
other columns from disk. The floppy must remain mounted during that
session. An unsaved new world continues to work without a floppy.

Travel reads use a cooperative controller state machine: motor spin-up,
restore, seek and track read return to gameplay while waiting. A command
owns the aligned DMA buffer until completion. Records can span physical
8 KiB tracks. The motor stays on during bursts and stops after inactivity.
A column is checked before its old resident owner is released.
The most recently read physical track remains cached in the existing DMA
buffer. Other records in that track copy directly without another read or
motor spin-up; spanning records reuse a cached prefix. Save/load buffer
reuse invalidates the track tag. This adds no second track allocation.

Version 4 saves use two complete snapshot banks, each 616 KiB. The new
bank's header is published after its payload writes succeed. Loading
checks both banks and chooses the newest valid snapshot; a damaged or
interrupted newer snapshot falls back to the older one. Each snapshot
includes terrain records, player/inventory state, chests, active animals,
dormant animals and population markers. Saves copy clean records from
the protected source bank without materializing all terrain in RAM.
Resident edits can be saved directly even when the dirty archive is full.

Changes remain pending until an explicit save; walking between regions
is not an autosave. Eviction compresses dirty columns into bounded RAM.
If that archive fills, data remains resident and the game reports the
problem. Saving commits those changes and frees the archive.

Existing version 1/2/3 saves import their exact blocks, rather than
regenerating terrain from a guessed seed. Old versions without mob
snapshots initialize regional animals after loading. An import exceeding
the archive budget returns an error rather than silently dropping blocks.

The existing 256-wide profile can exceed a snapshot bank. It keeps the
previous full-disk version 3 save layout and compressed RAM backing;
it does not get version 4 disk paging or the alternating-bank guarantee.
This preserves its larger save capacity while generation/storage evolve.

## Remaining limits

This pass establishes resident terrain streaming and mob region ownership;
it does not expand the logical world. The directory still has 256 entries,
several saved coordinates are bytes, and the dormant animal buffer holds
128 records. A full animal buffer retains active ownership instead of
losing an animal. Chests and dormant records still reside in bounded RAM,
although their complete state is persisted in saves.

The floppy's capacity and seek time remain real limits. A version 4
snapshot must fit one bank, including its global state. Increased RAM
headroom is not a measured renderer speedup, and no active mob cap,
view distance or resolution was raised automatically.

## Next generator: B, with shared region features

Use one world seed plus global coordinates for continuous height and
biome noise. Generate individual columns only when needed. Give features
such as cave systems deterministic region owners, and generate each
feature's portion in intersecting columns. Bounded neighboring feature
information keeps borders continuous without constructing a whole map.
Independent seeds for neighboring islands are insufficient on their own.

Store changed blocks and persistent entities, regenerate untouched
terrain, and keep imported legacy baseline records where their original
seed is unknown. Add a generation version to the existing seed metadata.
Generation and lighting must have bounded work budgets, and dirty records
must gain cooperative commits before the pending archive becomes full.
Region/chest/animal backing and the directory must also be paged before
removing the current finite bounds. Widen saved coordinates and audit
fixed-point distance calculations before increasing world extent.

A 1024 x 1024 map has about 114 times the columns of a 96 x 96 map. That
is a cell-count comparison, not a measured generation-time prediction.
Measure regeneration against floppy reads on the 16 MHz / 2 MB profile,
then choose how much generated terrain to retain in a small cache.

## Validation

- `mob_regions_test.c`: exact freezing, repeated ownership transfers,
  slot pressure, deaths, fuse behavior, population markers and IDs.
- `column_cache_test.c`: reference terrain/light, travel and eviction,
  movement prefetch, edits, torches, column serialization and failed
  eviction without data loss, on 2 MB and 4 MB budgets.
- `column_save_test.c`: real save/controller code with a DMA disk fixture,
  cooperative paging, empty dirty-archive saves, interrupted writes,
  damaged bank fallback, corrupt runtime reads, old-save import and the
  256-wide full-disk compatibility path.
  Four sample paging requests touching two tracks require two physical
  reads, and deferred streaming seams rebuild at most one mesh per frame.
- Existing mesh, cave, keyboard, movement and hostile-spawn regressions.
- `render_occlusion_test.c`: viewport pixels match the same renderer with
  floor culling disabled across 960 views, including cliffs, cave openings,
  transparent floors, entities and both resolutions. Missing terrain,
  unfinished meshes and pending edits cannot act as floor occluders.
- Tsugaru headless: 2 MB and 4 MB gameplay, actual floppy save, paged
  travel, reload, and another save while reading the protected source bank.

## Earlier renderer investigation

At the fixed-seed spawn, the earlier non-interlaced 2 MB benchmark averaged
17.0 FPS across eight directions. Texture unrolling measured 17.17 FPS;
a small edge cache 16.75; shared row setup 16.58; an opaque coverage prepass
16.71; repeated-texel runs 16.79. These single-run differences did not
establish a meaningful gain. None of those prototypes was shipped.
A larger edge cache shrank the generated map and changed the scene, so
its result was not a valid comparison. Future comparisons must keep
terrain, camera path, render settings and entity workload consistent.
The recent cache benchmark measured 16.875 FPS on the same finite terrain
and view settings; regional populations differ, and this is not a renderer
speedup claim.

## Boundary CPU measurement

A fixed-seed (4242) 2 MB / 16 MHz MODEL2 headless fixture follows a
32 -> 80 -> 32 X path at four blocks per second, with Z fixed at 48.
It renders normally at the existing view preset. Entity ticking is disabled
in this measurement fixture to isolate terrain preparation; terrain is
RAM-backed, so this measures CPU stalls rather than floppy service time.
The gameplay timer measures frame intervals at 10 ms resolution.

Before incremental initialization and optimized flood seeding, the route
had a 1,180 ms maximum frame and 1,020 ms 95th-percentile frame. After these
changes, they measured 230 ms and 150 ms, respectively. Median frame time
was 70 ms before and 60 ms after. Streaming-call time fell from 1,100 ms
maximum to 140 ms; individual mesh builds are now the main CPU spikes.
These are single-route measurements, not guarantees for every world,
camera, entity workload or floppy condition. The cache remains 25 columns;
these CPU improvements do not reduce its 600 KiB terrain/light allocation.

## Incremental meshes and downward views

Background mesh builds now scan four Y layers per frame, reusing the existing
temporary quad buffer. The renderer keeps the previous complete mesh until
the replacement is published. Block edits retain immediate rebuilds; edits,
light changes and column reuse invalidate affected partial jobs. Relighting
and new terrain share the mesh work budget even before a mesh is published.
Pool eviction retries publish the scanned quads without rescanning the chunk.

On the same 2 MB / 16 MHz boundary route, maximum frame time fell from 230 to
160 ms and the 95th percentile from 150 to 110 ms. The median remained 60 ms.
The maximum deferred-mesh work call measured 40 ms. These results include
normal rendering, frozen entities and RAM-backed terrain; they do not measure
floppy seek latency. No additional terrain, lighting or mesh buffer is allocated.

The previous hidden-cave shortcut applies only to sealed bottom chunks.
A steep downward view now checks a nearby opaque floor against the entire
viewport. If the four corner rays' floor footprint is covered by ready,
meshed opaque cubes, geometry wholly below that floor can be discarded.
The check is cached by camera pose and mesh version. A cave opening,
transparent tile, cliff within the footprint or unavailable mesh prevents
that floor from hiding geometry. The floor itself remains visible.

At the fixed-seed spawn with entities frozen and eight downward yaw angles
(pitch -250), the 4 MB / 25 MHz profile improved from 13.48 to 16.03 FPS.
Average drawn terrain faces fell from 187.5 to 120.2. The same 2 MB / 16 MHz
spawn measured 10.40 FPS before and after: its uneven ground did not provide
a usable floor for this shortcut. Forward-looking 2 MB performance measured
17.15 FPS against 17.17 before these renderer changes, within single-run
variation. The gain depends on the scene; it is not a universal FPS increase.

An opaque-face expansion closed the sky-colored cracks in enclosed-room
tests but reduced the stationary minimum-spec average by about one FPS.
It was removed at the user's request. Raster coverage remains unchanged,
so the existing sky speckles are still a known issue.

## Optional distance graphics presets

PF3 now cycles through the existing 8, 8/10 and 10/12 presets, then Distance
6/12 and Distance 6/16. The two new presets texture the first six blocks
and use the existing average-color flat shading beyond that. They retain
the full block meshes, cave openings and nearby texture subdivision.
The startup choices and existing presets remain the same.

Terrain already rejects back-facing faces before collecting draw items;
block models and rotated entity boxes also reject faces pointing away from
the camera. Faces between opaque neighbors are omitted during meshing.
Crossed plants are deliberately double sided. These checks cannot eliminate
faces that point toward the player but are hidden behind other terrain.

The distance presets add an exact face-bounds test against the expanded
view frustum, including each small piece of a subdivided quad. Invisible
pieces are rejected before projection and clipping. The flat raster loop
reuses a repeated color word and writes aligned spans inline, avoiding
per-row memset calls. No additional mesh, terrain or lighting cache is
allocated; the production kernel grew by 936 bytes from the previous build.

In the fixed-seed 4242 spawn fixture on Model 2 / 16 MHz / 2 MB, with normal
rendering, fixed entities and eight forward yaw angles, Distance 6/12
averaged 11.66 FPS. The same 6/12 cutoff with the previous draw path measured
10.56 FPS; the existing 10/12 preset measured 9.78 FPS. Frame-time median /
95th percentile / maximum changed from 100 / 110 / 120 ms to 90 / 100 /
110 ms against the same 6/12 cutoff. These are single-scene measurements,
not a universal speedup or a streaming-boundary benchmark.

The default 8-block setting measured 17.13 FPS against 17.15 before this
pass, with the same 50 ms median and 90 ms 95th-percentile / maximum frame
time. Distance 6/16 measured 7.65 FPS, with 130 / 150 / 180 ms frame times.
That setting is too costly for this minimum-spec scene and is offered for
faster profiles. The 12-block option increases reach at the expense of
texture detail and frame rate relative to the 8-block default.

The native renderer regression compares 1,920 distance-mode views against
the original draw path, including opaque roofs, cave openings, glass,
water, cliffs, two camera positions and both render resolutions. All
viewport pixels match. The existing 960 downward-occlusion views also pass.
The production 2 MB emulator smoke test checks all five PF3 presets, their
wraparound, both PF2 resolutions and return to the 12-block distance preset.

## Optional general terrain occlusion

PF8 toggles broader terrain occlusion culling. It starts off and works with
every view preset. A 20-by-13 coverage/depth map uses 1,560 bytes, plus small
metadata. At most 16 large opaque terrain patches within six blocks enter
the prepass. Water, glass, cutout leaves and block/entity models do not act
as occluders. Models and entities can still be rejected when hidden.

Coverage follows the existing rasterizer's actual edge rounding and guard
clipping. A tile becomes valid only when one opaque patch covers every
pixel in all of its rows. Linear edge extrema are evaluated across each
row band instead of walking every scanline. Partial tiles and cracks remain
unknown. A target's whole projected bounds must be covered, its nearest
depth must be behind every covering patch's farthest depth, and painter
order must prove those patches draw afterward. Otherwise it is rendered.

The proof map is reused only for an identical camera pose, render scale,
view distance, floor shortcut and mesh version. Wholly hidden static items
reuse spare bits in the existing draw list; rebuilding the proof clears
them. Block edits and streaming mesh publication invalidate the proof via
mesh version changes. Moving entities keep per-frame visibility tests.
The production kernel grew from 175,668 to 179,684 bytes in this pass;
the map's static storage is additional to that code growth.

With seed 4242, normal rendering and frozen entities on Model 2 / 16 MHz /
2 MB, Distance 6/16 improved from 7.46 FPS with culling off to 8.45 FPS on.
Average drawn faces fell from about 300 to 237. The frame median remained
130 ms; the 95th percentile improved from 160 to 150 ms, while the maximum
rose from 180 to 190 ms. This remains below the desired minimum-spec frame
rate and does not make 16 blocks a recommended default.

An earlier 48-patch prototype at Distance 6/12 measured 11.40 FPS off and
11.77 FPS on in the stationary fixture, but made the moving boundary route
about 2% slower. With the reduced 16-patch budget and hidden-item cache,
the 12-block RAM-backed route measured 280 frames off and 282 on over about
24 seconds. Both had an 80 ms median and 190 ms maximum; the 95th percentile
was 140 ms off and 150 ms on. That is no meaningful overall walking gain.
These route results precede a correction that clamps the candidate-size
filter to the viewport to avoid integer overflow for near-clipped faces.
The default 8-block setting with the final implementation disabled measured
17.11 FPS, with the same 50 / 90 / 90 ms frame-time statistics as before.

`render_general_occlusion_test.c` compares 5,184 views with culling on/off
using patterned textures, roofs, cliffs, cave interiors, wall openings,
glass/water, rotated entities, 8/12/16-block distances and both resolutions.
Every viewport pixel matches. It also checks exact-pose reuse, small camera
movements and opening a wall after cached hidden items have been recorded.
The existing 960 floor-occlusion and 1,920 distance-mode views still pass.
