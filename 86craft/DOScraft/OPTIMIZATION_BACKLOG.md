# DOScraft optimization backlog

**2026-10-10 authorization update:** the user now requests measured optimization
implementation and independent prioritization beyond this list. That overrides
the original Phase B timing gate below; it does not declare full port parity
accepted. See [PERFORMANCE.md](PERFORMANCE.md) for the automated guest replay,
measurement limitations, independent ranking and retained experiment results.
The follow-up request explicitly groups graphics into Low/Medium/High, with
adaptive subdivision in **all** profiles. Modest visual tradeoffs may be
considered; substantial visual/gameplay changes require approval first. Invisible
changes are slightly preferred, not a reason to ignore larger measured gains.
See [FRONT_TO_BACK.md](FRONT_TO_BACK.md) for the ordered-coverage experiment.

**Visual-tolerance clarification (2026-10-10):** bit-identical output is not a
blanket optimization requirement. Small, even slightly noticeable image-quality
tradeoffs are authorized, and may be gated behind Low/Medium; High can retain the
cleaner reference path. Adaptive subdivision remains selected for all profiles.
Evaluate measured moving-scene gains against the actual visual differences.
Keep reviewed image evidence, hardware readback, memory safety and gameplay
correctness checks. Major visual/functionality changes still require asking.

Recorded 2026-10-09 from the user's complete `Pasted text.txt` attachment and the existing Towns source/retention audit. This is **Phase B research after the user accepts the complete Phase A direct port**, not a list of prerequisites for porting. [PORT_PLAN.md](PORT_PLAN.md) defines the full parity gate. No proposal here is a selected implementation, a measured DOS gain or a promised FPS result.

Latest resolution guidance: the user authorizes choosing whichever resolution makes sense for this PC. The existing 320x240 layout is the direct-port starting point, not a permanent restriction. First candidate: Mode X display, sharp HUD, existing doubled 160x100 3D option. Compare full-resolution rendering and presentation costs once playable; do not infer a winner from the nominal VGA mode alone.

Phase A preserves every existing gameplay/feature/storage behavior and inherited optimization method. Required DOS platform adapters, optional profiling, repeatable routes and correctness/reference harnesses are allowed port infrastructure. A small playable slice, profiling result or performance shortfall does not authorize starting production optimization work early. Record findings now; implement new performance changes only after parity acceptance, in small coherent commits owned by the main integrator.

## Fixed target and corrections to the supplied list

### Current independent ranking (2026-10-10 continuation)

Evaluate this backlog alongside actual profiling; its original order is not a
requirement to retry losing candidates. The original painter's coarse drawing
samples put about 53% in opaque trap assembly, 16-18% in polygon setup and
11-13% in quad generation. Coverage run discovery and orchestration outweighed
avoided drawing in both exact reverse-painter prototypes. These are function
samples, not proof of which instructions or cache misses dominate.

1. **Opaque texel-loop overhead and useful row invariants.** Finer samples
   attribute about 25% of drawing to trap row setup and 29% to trap texel loops.
   Independently linked assembly parity now protects both. Destination-register
   reuse passes Low images but saves only about 0.5% in the house, mostly neutral
   elsewhere; High house saves only 0.2%, with other scenes nearly neutral.
   The separate exact-zero gradient probe
   also saves about 0.5% in the Low house, with neutral/slightly slower other
   scenes; neither is adopted from that result. Wider row-gradient reuse
   needs measured eligibility/hit rates, exact shifted-delta/reciprocal keys and
   accounting for comparison/dispatch/cache costs; no blanket per-face gradient.
   The new exact constant-gradient entry specialization is effectively neutral
   on Low and stays off. Four-texel opaque unrolling adds only 288 bytes, passes
   independent actual assembly parity, and now saves about 1.1% Low house frame
   time and 2.7% High house / 1.4% High walking in isolated guest repeats.
   Combined normal-game verification/publication remains a separate gate.
2. **Earlier exact visibility/geometry rejection with cheaper maintenance.**
   Target hidden building geometry before projection/subdivision/setup. Do not
   stack the losing per-pixel coverage backend onto the painter, or invent
   filled coverage from a bounding rectangle. Keep doorway/model/cutout oracles.
   The directional world-space per-item wall-cone probe also loses: Low house
   +12% time, walking +14.4%, down +38.2%, despite matching images. Its guarded
   house eligibility is only zero/one rejected item per frame for 628-2,642
   tests. Leave it off. Next work needs cheap group/batch-level rejection and
   eligibility counters before adding per-item costs.
3. **ET4000 banked chunky CPU upload, now accepted for High.** The independent
   High-only path removes planar conversion and exercises contiguous VGA
   writes. Low/Medium keep the accepted repeated-pixel broadcast. Same 320x240
   timing, palette, triple buffering and sharp HUD; no Mode13h crop or renderer
   change. Native independent plane/bank tests and actual guest readback/timing
   must establish correctness and benefit. Low differs on five doubled logical
   texture-edge pixels in the down scene, reproduced in all repeats; root cause
   is not established. This small reviewed difference no longer blocks High
   testing under the user's clarified visual tolerance. High now completes
   three PASS repeats with matching images: 2.4% lower walking frame time,
   10.3% looking up, 1.5% house and 0.8% looking down; static/edits neutral.
   Both isolated and exact-release game-loop preset/save/reload smoke pass.
   It is enabled in normal defaults and the save-preserving published ISO.
   See PERFORMANCE.md for fingerprints and preserved player HDD/previous ISO.
   A separate direct pitched-row upload prototype avoids full-frame RAM
   gathering while switching banks only at real crossings. Native presenter
   tests pass; three High repeats save about 0.5-1.6% in moving/edit scenes,
   with neutral Low timing and matching images in both profiles. Combined
   production timing and normal-game verification remain adoption gates.

Stationary-only reuse and already-rejected edge reciprocal/word transpose
variants are lower priority. x87 conversion, reduced visible content, spawning
changes and world-storage redesign are not incidental fixes. Every measured
comparison now includes average FPS, slow-tail FPS and sample-support flags;
32-frame screens cannot establish statistically useful 0.1% lows.

Next bounded probes should include both invisible and quality-gated options:

- Independently investigate **fused Low plane packing/broadcast classification**.
  `gfx_present` supplies only a render-scale hint, not a proof that every
  view pixel is doubled: menus, crosshairs and sharp text may violate it.
  The current presenter packs all four planes with `dos_vga_pack_all`, then
  scans packed rows again with `dos_vga_upload_plan`. Detect equal pairs
  during packing, retaining exact arbitrary-pixel row fallbacks and the sharp
  per-page HUD, rather than blindly discarding planes/odd rows. Measure the
  cost of validation/fallback versus avoided stores/scans; the prior losing
  word-transpose probe is not evidence that this fusion helps. This targets
  Low's remaining roughly 8.5 ms pack section, not just stationary reuse.
- Independently reuse the accepted word-wise HUD identity helper for the
  banked High path. The current chunky HUD check uses the imported byte-loop
  `memcmp` over 12,800 bytes; direct-row timing still spends about 5.2 ms in
  the pack/HUD section on its first repeat. Four 3,200-byte helper calls for
  the staged path (or 40 exact 320-byte row calls for a pitched path) could
  reduce that cost without visual or hardware changes. Do not combine it with
  the direct-row trial before independent measurement. Existing helper tests
  cover unaligned loads, no-overread guards, changed bytes and exact shadows.
- Parent-local shared-corner projection reuse was implemented and tested but
  worsens Low down timing by 0.8%; preserve its checkpoint off. The bounded
  depth-aware 2x2 opaque adaptive-leaf quality probe is also neutral; it does not
  justify a quality tradeoff. Do not retry either unchanged as a top candidate.
- Low/Medium texture-gradient amortization over short row groups. Unlike exact
  reuse, approximate gradients may slightly change texture samples; retain
  geometry, clipping, cutout semantics and the High reference path. Review
  motion/near-texture artifacts and native bounds tests alongside guest timing.
  Adaptive subdivision remains enabled. This is a candidate, not a selected
  precision rule, implementation or measured gain.

### Set-aside candidate: bounded sealed-room visibility proof

The user prioritizes rooms with doors/doorways and rejects changing the existing
benchmark to favor a rare sealed-room case. The incomplete draft is retained
unintegrated in `research/sealed_room/`; it is not the next selected optimization.
Directional wall rejection should be useful when facing walls while retaining
everything visible through the entrance. No benchmark fixture was changed.

A bounded flood of camera-connected passable cells could prove a small
room completely enclosed by full opaque cubes. Then chunks wholly outside the
room's conservatively padded bounds could be rejected before face collection/
projection. Keep boundary/overlapping merged faces, models/entities and the
original painter initially. This applies while moving indoors, not only when
stationary. It is a proposed experiment, not implemented or a promised gain.

Source audit exposes important hazards: `wget` returns bedrock for an unloaded
column, so that must NOT be accepted as an opaque enclosure. Check readiness
explicitly and abort on unknown/boundary/overflow. Collision solidity is not an
occlusion predicate; only full `BF_CUBE|BF_OPAQUE` cells can seal the proof.
Doors/models/glass/leaves/water must not silently seal holes. `world_set` changes
blocks before deferred meshes increment `g_meshVersion`; mesh version alone is
NOT enough cache invalidation. Require immediate edit/residency/publication
invalidation and fail open while boundary geometry is unpublished.

Cheap open-sky/height checks and cached failed traversals are necessary to avoid
paying a flood every outdoor frame/cell. Measure flood cost and edit latency as
well as rejected geometry. Test sealed rooms, open doorways/windows, cutouts,
camera movement, stream seams and edits with an independent visibility oracle.
Open rooms/caves or inconclusive floods retain the original renderer. A later
portal system would be a separate, substantially larger candidate; do not
confuse simple connectivity with a general visibility proof through openings.

- Primary hardware remains 486DX/25 with integrated x87, 16 MB RAM, **ISA Tseng ET4000AX with 1 MB**, WD1007V-SE1 ESDI / desired CDC Wren V 94186-383, and original Sound Blaster 1.x / OPL2. The 386DX/33 compatibility port is deferred until the main demake is satisfactory.
- Preserve the **320x240 game UI**, including the 200-row view and 40-row HUD, and existing viewport/scale options. The attachment's 320x200 Mode13h examples are useful diagnostic arithmetic, not authority to shrink the game. An 8-bit full 320x240 color image is 76,800 bytes; its full-screen coverage bitset is 9,600 bytes. A 320x200 3D-only image/bitset is 64,000/8,000 bytes; a logical 160x100 image/bitset is 16,000/2,000 bytes. State which surface is measured and account separately for HUD, pitch, planes, buffers and metadata.
- Correct the attachment's IBM PS/2 VGA/MCA guidance to the agreed **ISA ET4000AX**. Measure its actual planar/presentation path and ISA transfer costs; do not substitute MCA, VLB, W32 or PCI behavior.
- Pinned official 86Box v6.0 b9001 lacks the `CDC94186383` timing preset found in the newer fork. It silently rewrites that unsupported value to RAM Disk. Diagnostic profiles now use supported `1989_3500rpm`, a **generic 1989 timing proxy**, with a temporary **20 MiB scratch disk**. Preserve the desired final ESDI/Wren hardware; these diagnostics do not establish final capacity/timing fidelity. The launcher rejects mismatched hardware/media and RAM Disk timing. A visible launch reached BIOS and its first-boot CMOS setup; DOS/probe execution remains unverified after the user stopped computer-use testing.
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
