# DOScraft optimization backlog

Recorded 2026-10-09 from the user's complete `Pasted text.txt` attachment and the existing Towns source/retention audit. This is **Phase B research after the user accepts the complete Phase A direct port**, not a list of prerequisites for porting. [PORT_PLAN.md](PORT_PLAN.md) defines the full parity gate. No proposal here is a selected implementation, a measured DOS gain or a promised FPS result.

Phase A preserves every existing gameplay/feature/storage behavior and inherited optimization method. Required DOS platform adapters, optional profiling, repeatable routes and correctness/reference harnesses are allowed port infrastructure. A small playable slice, profiling result or performance shortfall does not authorize starting production optimization work early. Record findings now; implement new performance changes only after parity acceptance, in small coherent commits owned by the main integrator.

## Fixed target and corrections to the supplied list

- Primary hardware remains 486DX/25 with integrated x87, 16 MB RAM, **ISA Tseng ET4000AX with 1 MB**, WD1007V-SE1 ESDI / desired CDC Wren V 94186-383, and original Sound Blaster 1.x / OPL2. The 386DX/33 compatibility port is deferred until the main demake is satisfactory.
- Preserve the **320x240 game UI**, including the 200-row view and 40-row HUD, and existing viewport/scale options. The attachment's 320x200 Mode13h examples are useful diagnostic arithmetic, not authority to shrink the game. An 8-bit full 320x240 color image is 76,800 bytes; its full-screen coverage bitset is 9,600 bytes. A 320x200 3D-only image/bitset is 64,000/8,000 bytes; a logical 160x100 image/bitset is 16,000/2,000 bytes. State which surface is measured and account separately for HUD, pitch, planes, buffers and metadata.
- Correct the attachment's IBM PS/2 VGA/MCA guidance to the agreed **ISA ET4000AX**. Measure its actual planar/presentation path and ISA transfer costs; do not substitute MCA, VLB, W32 or PCI behavior.
- Pinned official 86Box v6.0 b9001 lacks the `CDC94186383` timing preset found in the newer fork. It silently rewrites that unsupported value to RAM Disk. The main owner is correcting diagnostic profiles to supported `1989_3500rpm`, a **generic 1989 timing proxy**, with a temporary **20 MiB scratch disk**. Preserve the desired final ESDI/Wren hardware; these diagnostics do not establish final capacity/timing fidelity. Validate the config written by the runtime before using disk results. Guest startup currently hangs without a window; boot/probe execution remains unverified.
- An available x87 and compiler permission to emit 486/x87 instructions do not request conversion of existing integer algorithms. All new x87 conversion experiments belong to Phase B.

## Investigate first: the user's ordered ten

Ranks 1-3 are **one rendering architecture group**, not three unrelated patches. Prove ordering first, compare coverage representations second, then measure the incremental benefit of tile hierarchy. Keep each step independently selectable and measured; do not simultaneously land every overlapping technique.

| Rank | Investigation | Existing baseline / decision gate |
| --- | --- | --- |
| 1 | Correct front-to-back opaque traversal that permits early geometry rejection | Current renderer is far-to-near painter ordering. Prove near-to-far ordering for merged/clipped terrain; block-center distance sorting is insufficient. Resolve models/cutouts with depth or a separate ordering proof. |
| 2 | Scanline uncovered spans versus compact per-pixel coverage bitmask | Neither full coverage architecture is established by the existing conservative prepass. Compare cycles saved against interval fragmentation, mask tests, memory and fallback cost. |
| 3 | Hierarchical screen-tile occlusion rejecting blocks/chunks | Current optional coarse single-patch proof is not cumulative hierarchy. Measure tile maintenance/rejection cost after ranks 1-2; conservative bounds must preserve openings. |
| 4 | Internal-face elimination and cached chunk meshes before transforms | Already substantially implemented. Preserve/audit it; investigate only demonstrated gaps, cache invalidation or avoidable per-frame work. |
| 5 | Greedy meshing to reduce vertices/setup | Already implemented as horizontal greedy merges and side runs, with texture/light compatibility. Measure possible extensions and near-subdivision tradeoffs rather than claiming a new method. |
| 6 | Incremental grid transforms | Per-axis additive camera/grid and cloud tables already exist. Profile remaining duplicate transforms and changes to reuse, not a wholesale replacement by default. |
| 7 | Direct quad/span rasterization instead of independent triangles | Existing merged-face/polygon trapezoid/span path already exploits quads. Audit clipping/subdivision setup and benchmark any additional specialization. |
| 8 | Fixed-point texture spans, reciprocal/perspective tables and specialized assembly | Fixed-point affine stepping, reciprocals and assembly fast paths already exist. Perspective correction/new 486 specialization are separate measured extensions. |
| 9 | System-RAM framebuffer + optimized VGA upload versus direct rendering | Towns direct VRAM/triple buffering is inherited hardware-specific behavior. Phase A needs a compatible presentation adapter; competing performance paths wait for Phase B and must retain 320x240 UI. |
| 10 | 486 cache-aware layouts/code organization | Compact/aligned textures and some buffers already exist. Measure total hot code/data against the 486's 8 KB unified cache; specialization can make it worse. |

A binary coverage mask contains no depth. A covered pixel is safe to skip only when no later geometry can be nearer. Bounds rectangles are not filled-pixel coverage; transparent texel zero must not write color/depth/coverage. Moving/intersecting models, near-plane intersections, glass/water/foliage, doors, shared edges and edit invalidation must remain correct. Span-list overflow needs a bounded correctness-preserving fallback. An independent depth reference is useful infrastructure, not automatic selection of a production Z-buffer renderer.

## Audit of already implemented methods

Evidence: [existing optimization retention audit](../../fmtowns/townscraft/OPTIMIZATION_AUDIT.md), plus read-only inspection of the source files below. This records Towns implementation, **not proof that the DOS port already includes or benchmarks these methods**. Recheck against the pinned Phase A import and its tests. “Partial” means related mechanisms exist but do not implement the whole proposal; “not established” is a research item, not proof that no related code exists.

| Attachment methods | Source evidence / inherited status | Port or later action |
| --- | --- | --- |
| Chunk/group frustum, face backface, internal faces, distance rejection | `render.c` collection/group bounds, facing and distance tests; `world.c` exposed-face mask scanner. Existing floor rejection also remains. | Preserve behavior and inherited graphics-option defaults in A; finer block tests only if B profiling justifies them. |
| General/early chunk or face occlusion | `render.c` optional/default-off prepass, at most 16 opaque patches, single-patch full-tile proofs, conservative depth/order bounds, exact-pose proof reuse. Partial. | It is not reliable cumulative front-to-back coverage, uncovered spans or multilevel hierarchy. Preserve in A; ranks 1-3 investigate replacements in B. |
| Cached meshes, greedy meshing, dirty regions/incremental rebuilds | `world.c` `greedy2d`, side runs, `scan_chunk_mask`, dirty layers, seam invalidation and immediate ordinary edit geometry; cached sorted terrain list in `render.c`. | Preserve in A. Broader merges, local publication versions or different allocation/rebuild units need B evidence and seam/light/edit tests. |
| Incremental edges/UV, affine mapping, quad/span paths | `raster.c` / `trap.S` polygon clipping/trapezoid spans, fixed-point interpolation and reciprocal gradient setup. Existing near subdivision reduces affine distortion. | Preserve in A; interval perspective correction, new column paths or setup sharing are B candidates. |
| Assembly, branches/calls/registers, division/addressing fast paths | `trap.S` two-pixel loops, no-wrap path, register UV/address stepping; `raster.c` `g_recip14`; `render.c` `build_inv_table` / reciprocal projection. | Adapt ABI/object format in A. Audit remaining divisions/loads/calls; no claim that every hot loop is already optimal. |
| Indexed textures, atlas/locality/alignment, palette lighting | `textures.c` indexed palette ramps and `g_shadeLUT`; `trap.S` 16x16 addressing with 256-byte atlas stride and 4 KiB page alignment. | Preserve texture/light identity in A. New packing/preconversion/SoA/alignment policy is B, with total cache footprint measured. |
| Shared/incremental transforms, camera coefficients/trig, early bounds | `render.c` additive `TX/TY/TZ` grid tables, `setup_camera`, tabulated frustum planes, early face/model rejection; `fmath.c` lookup math. | Preserve in A. Audit reuse scope and clipping/precision fast paths before extending. |
| Compact blocks, contiguous residents, scratch/work reuse | `world.c`, `column_cache.inc`, `column_store.inc` already separate resident one-byte block/light data, column slots and reusable mesh/light work. Partial. | A retains current ownership/limits. New SoA, chunk-local layout, memory pools or larger-world coordinates are B redesign. |
| Fixed timestep, distant AI, block-grid collision, cuboid entities | `game.c` fixed 20 Hz ticks from 100 Hz timer with capped catch-up; `mobs.c` far quarter-rate AI, hostile despawn/dormant regions and render boxes; `physics.c` local block-box queries. | Preserve cadence, hitch behavior, spawning/persistence and animation in A. New entity partitioning, batching/prioritization or lower update rates are B and must preserve rules. |
| Incremental light, sunlight/heightmaps, day/night | `world.c` flood-fill/remove propagation, height map and column skylight; palette/face lighting and sky-darkening in `textures.c` / `game.c`. | Preserve in A. Measure any further scheduling/cache change in B, including deferred light and affected meshes. |
| Workload budgeting, generation/noise caching, storage | `world.c` / column includes have bounded streaming/mesh/light work and cached value-noise lattices; `save.c` has durable serialization and streamed column reads distinct from temporary HDD backing. Partial. | A adapts I/O while keeping existing seeds/state/save semantics. New local generation, seed-plus-edits formats, incremental save policy or storage architecture are B. |
| Presentation/HUD caching | `video.c` / `gfx.c` Towns direct VRAM triple buffering; `ui.c` per-page HUD caching and frozen menu backgrounds per retention audit. | Retain visual behavior, adapt hardware in A; Towns timing does not establish ISA VGA gains. |
| Front-to-back coverage, hierarchical connectivity/portals, projected-size rejection, x87 conversion | Full implementations are not established by this audit. Removed interlacing is deliberately absent. | Deferred B research; do not restore interlacing or alter visible content as an assumed parity requirement. |

## Remaining attachment inventory by area

The ordered ten above establish priority, while these groups retain the rest of the user's list. Preserve already implemented behavior in A; every extension listed here is Phase B unless it is solely profiling/correctness infrastructure.

### Visibility, occlusion and geometry elimination

- Compare voxel DDA, octant or ordered-chunk camera-relative traversal with a proven near-to-far ordering. Transform only potentially visible geometry; test chunk bounds first and finer block/face bounds where useful.
- Combine early chunk and face rejection with span intersection against uncovered scanlines, full-tile hierarchy and whole-viewport termination once opaque coverage is complete. Include sky/background rules and avoid treating HUD pixels as world coverage.
- Audit existing frustum/backface/internal-face/distance culling and greedy/cached mesh behavior before adding tests. Visibility-aware traversal may avoid terrain behind opaque surfaces only with conservative proofs.
- Investigate chunk boundary air-connectivity and cave/indoor portal traversal, with dirty metadata updates for edits and conservative unloaded-neighbor behavior. Compare their maintenance/traversal cost to rejected work.
- Projected-size culling or selective affine/animation simplification can change the image; establish acceptable error with the user before adopting a visible quality change. Preserve render-radius defaults and full caves/vertical looking.

### Rasterizer and presentation

- Retain 8-bit indexed output and the game layout. Compare conventional/system RAM rendering plus sequential planar VGA upload to direct VRAM; account for complete view/HUD bytes, page selection and retrace. A “single 64,000-byte copy” only describes a 320x200 packed surface, not necessarily the actual presentation.
- Audit fixed-point transforms/projection/UV, incremental edge and UV stepping, direct quads/spans, specialized cube faces and shared triangle/plane/edge setup. Test cracks and clipping rules before adopting fewer setup operations.
- Benchmark perspective-correct UV intervals of 4/8/16 pixels, reciprocal tables/refinement and numerically safe division elimination against current affine subdivision. Selective affine mapping on small/distant faces requires an agreed visual tolerance.
- Audit 16x16 power-of-two texture addressing, sampling/orientation/light specialization and existing assembly loops. Only demonstrated hotspots justify new 486 assembly, inlining, branch hoisting, register allocation or reduced reload/memory dependencies; compare against a C/reference path.
- Compare known-visible write-only spans, column rasterization for suitable faces and clearing only needed background pixels. Avoid unnecessary framebuffer reads/VGA read-modify-write, but preserve sky, cutout holes, overlays and every reused-buffer pixel.

### Cache, memory and transfer

- Measure the 8 KB unified L1 hot working set: raster code, frequently sampled textures, lookup/lighting tables, compact face/vertex/raster scratch and code specialization together. Group/pack atlases and final indexed textures only when locality/addressing benefits are demonstrated.
- Compare SoA versus current compact layouts, contiguous chunk storage, pointer chasing and cache-friendly traversal while respecting visibility order. Audit inherited dirty-neighbor tracking/incremental chunk updates before changing rebuild policy.
- Reuse startup scratch buffers; compare alignment, fixed pools and REP MOVSD versus other aligned bulk-copy paths. Measure framebuffer bandwidth separately from computation; optimize for the actual 486 and ISA path.
- Avoid frequent VGA bank switching using a compatible 320x240 mode/presentation design. Mode13h's convenience does not authorize forcing the game to 320x200. Record plane/bank/write costs and any read-dependent rendering paths.

### Transform and projection

- Extend shared-vertex or camera-space/reciprocal-depth reuse only where the existing additive grid tables leave duplication. Compare chunk-relative/local coordinates and transformed-axis-vector stepping with current precision/overflow bounds.
- Audit per-frame rotation coefficients, lookup trig and face-orientation IDs before proposing new tables. Integer camera/screen bounds must remain conservative; fully visible near-plane fast paths must match general clipping.
- Choose narrower fixed-point formats only after range/overflow and visual-error tests. Batched x87 transforms/projection/perspective are separate B experiments, including conversion/memory cost and tolerances; no assumption of a universal FPU gain.

### World simulation, generation and saving

- Preserve inherited fixed ticks, distant AI, grid collision and existing dormant/persistent state. Investigate creature spatial partitioning, visible-entity animation priority, shared cuboid motion templates and batching only if profiling shows a benefit without ordering/rule changes.
- Audit incremental light, sunlight/column heightmaps and day/night brightness before changing update intervals, palette handling or propagation work. Validate edit lighting and seams.
- Investigate deterministic resumable workload queues for generation/rebuild/noncritical work, bounded incremental generation and additional procedural-noise caching. Compare frame spikes as well as throughput; DOS disk calls remain synchronous and cannot be interrupted by a CPU budget.
- Larger-world region stores, local deterministic generation, seed-plus-modified-chunk compact saves and incremental saving are distinct B redesigns. Preserve all authoritative edits, player/container/mob state and supported save behavior; audit reload, interrupted writes, disk-full and format/generator compatibility. Do not substitute temporary scratch backing for durable saves or introduce ownership/spawning UI as an optimization.

## Measurement, correctness and adoption gates

Phase A may build this infrastructure and record the baseline. In Phase B use it to evaluate one independently selectable candidate at a time; infrastructure itself is not a shipping speedup.

- Separate traversal/culling, transforms, clipping, setup/spans/depth/texturing, framebuffer conversion/upload/wait, simulation, light/mesh/generation and I/O. Count submitted/culled/rasterized faces, samples versus nonzero writes/unique pixels, overdraw, and occlusion-test cycles versus saved work. Measure counter overhead with profiling disabled/enabled.
- Version the source/binary, exact emulator/ROM/CPU/FPU/RAM/video/disk/runtime profile, seed/world hashes, view/scale/texture settings and route. Score guest CPU time on the **486DX/25**, checking host emulation speed; modern host loop speed is not target evidence. Reject RAM Disk fallbacks as ESDI timing evidence.
- Include dense forests, caves, plains, mountains, complex buildings/interiors and groups of mobs, plus static/moving camera, full up/down pitch, open terrain, a hidden dense world behind an opaque wall, cutouts/doors/models/near clipping, edits/chunk seams, cold/warm disk traversal/revisits, save/reload/recovery and audio-on/off.
- Record median/p95/p99/max frame time and long-frame frequency, edit-to-present latency, peak RAM, queue debt/overflow, disk requests/bytes/latency and audio underruns. Separate startup cooking from steady-state rendering. Run at least three repeats with uncertainty and per-scene regressions, not an FPS-only aggregate.
- Preserve inherited correctness suites and add independent depth-reference/golden/replay checks for new rounding/ordering. Conservative culling must never lose visible geometry; cutout zeros write nothing; stale coverage/meshes after edits, motion or publication are failures. Keep the accepted painter/simple fallback for debugging and comparison.
- Profile early-rejection overhead and each optimization independently. Compile-time material/direction/mode specialization must include code-size/cache costs. Coverage spans, bitmasks, tiles, greedy meshes and specialized rasterizers overlap; avoid accumulating speculative mechanisms without evidence.
- Each experimental commit should describe the hotspot, hypothesis, baseline/candidate hashes, correctness checks, measurements, uncertainty/regressions and keep/revise/reject decision. Small independent commits make attribution/reversion possible; the main owner stages/commits them.
- Preserve the accepted primary settings/content. A meaningful primary-baseline regression rejects a candidate; neutral results within noise can remain only when a repeatable higher-setting benefit and acceptable complexity justify them. Roll back losing experiments or retain them only as explicit test candidates, not silent production defaults.

The attachment's 20-30 FPS discussion is motivation, not evidence or a guarantee. The plan's provisional 15 FPS house/walking and stall goals are likewise measured Phase B targets, not permission to reduce Phase A parity. No optimization, cache increase, x87 use or proposed renderer architecture has a guaranteed gain on this machine.
