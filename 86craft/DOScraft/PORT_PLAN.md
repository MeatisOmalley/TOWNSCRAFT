# DOScraft: Townscraft-to-IBM-PC port plan

Research and investigation: 2026-10-09. This is the architecture/measurement phase, not a working DOS game. No production Towns gameplay, UI, renderer or emulator behavior was changed for this plan.

## Recommendation

Build a real 32-bit DOS executable running on an emulated late-1989 386DX/33 PC. Keep 86Box as the hardware emulator, not the game engine. Reuse gameplay and content, establish a comparable painter-renderer baseline, then compete that baseline against a new visibility-first software renderer. Use the HDD for durable world regions and RAM for the active neighborhood. Do not carry over the Towns whole-world allocation, floppy backing, hardware drivers or packed absolute rendering coordinates.

The leading renderer candidate is **opaque front-to-back visible spans/coverage, with depth information for cutouts and moving models**. This is a hypothesis to benchmark, not an already selected implementation or promised speedup. A conventional fixed-point Z-buffer implementation is the correctness reference and a serious competing candidate. The shipping winner is determined by dense-house frame times, outdoor costs, correctness and memory use on the target CPU.

The first playable milestone is a small deterministic world with walking, full up/down looking, block placement/removal, inventory and unchanged controls. Large worlds and full sound follow after the DOS platform and house-rendering problem are measured.

## 1. What the investigation establishes

### Source findings

- `fmtowns/townscraft/src/render.c` draws a radix-sorted **far-to-near painter list**. Hidden geometry can still be collected, transformed, clipped and textured before a nearer house wall overwrites it.
- Frustum, backface, group/chunk and downward opaque-floor rejection already exist. This is not a game with no view culling. The ordinary draw path also performs bounds rejection; the distance presets add earlier per-piece checks.
- `draw_quad_split()` subdivides nearby merged quads to mitigate affine texture distortion. Large walls/ceilings close to the camera can therefore cost several polygons despite a compact greedy mesh. Added geometry, subdivision, clipping and hidden overdraw are separate hypotheses.
- General occlusion is optional/default-off. Its prepass accepts at most 16 nearby opaque patches; a coarse tile is covered only if **one patch** covers the entire tile. Several partial patches do not combine into a proof. It also runs through the nearby subdivision path, which can reduce usable patch coverage. Whole projected bounds plus conservative depth and painter-order tests must pass before rejection. This favors correctness but can miss many useful interior occlusions.
- Its proof cache is exact-pose-only. Walking/turning rebuilds the proof; standing still can reuse it. Benchmark both.
- `raster.c`/`trap.S` transparency means “skip texel zero,” not general alpha blending. Glass/water/foliage holes must remain visible. Models/entities have additional ordering constraints.
- Block edits also trigger lighting and dirty-layer mesh work, followed by face-list invalidation. Their spikes must be separated from the steady cost of looking at an already completed house.

Earlier reports rejected some prototypes using open spawn or travel fixtures. `STREAMING.md` records an opaque prepass at 16.71 versus 17.0 FPS, and an older 48-patch version slowing travel about 2%. Those results do not demonstrate that every visibility-first design loses in a dense interior. They do justify keeping an inexpensive outdoor path and measuring all overhead.

### New controlled house experiment

`tools/audit_towns_house.py` creates isolated instrumented source snapshots and runs the existing Tsugaru guest-clock benchmark. The source revision is `32133947eadc33d437f962162deda5df606d37cf`; exact source and ISO hashes are recorded in the results. The house is a 17-by-13 shell with doorway, glass window and roof, added to the same deterministic cave/tree/background terrain. The “no house” run uses the same camera locations. Geometry and lighting are cooked before sampling; there are no building edits, AI or HDD accesses.

First-run results, MODEL2 / 16 MHz / 8 MB, logical 160x100 3D viewport, 12-block view, fully textured distance, eight sampled seconds per phase:

| Camera phase | No house, culling off | House, culling off | House, existing culling on |
| --- | ---: | ---: | ---: |
| Outside static | 23.15 FPS | 10.95 FPS | 10.54 FPS |
| Inside static | 14.56 FPS | 10.93 FPS | 10.70 FPS |
| Inside moving/turning | 13.17 FPS | 10.07 FPS | 8.85 FPS |
| Inside looking up | 43.51 FPS | 14.95 FPS | 14.68 FPS |

For the interior static case, mean renderer time rises from 68.24 to 90.94 ms while update work is only 0.19 versus 0.33 ms. For looking up, renderer time rises from 22.53 to 66.44 ms with update work about 0.19 ms in both. Rasterized faces rise from 85 to 134 in the static interior and 24 to 79 looking up. Textured-span sample counts rise approximately 31% and 6.1 times, respectively; these counters include transparent span samples and exclude flat/tiny/sky work, so they are **not** a complete opaque-overdraw ratio.

This reproduces a completed-house rendering penalty independently of block-edit stalls or disk speed. The present conservative prepass is not the solution in this fixture: moving-house renderer time rises further to 112.51 ms. It does not prove that unnecessary overdraw alone accounts for all the cost, or that a front-to-back rewrite will win. Stage-level profiling is the next experiment.

Limitations: one run per case, a synthetic house rather than the user's house/seed, a 12-block test setting rather than the default 8, frozen simulation, and no PC/VGA port. Outside sampling still includes some mesh preparation, so it is not a pure steady-state render comparison. Inside phases are substantially settled. Motion is prescribed from emulated time, so variants sample slightly different points along the same route. No statistical significance or exact Everex FPS is claimed. The historical 2 MB run is almost identical (house moving: 10.07 FPS off / 8.85 on), so more RAM by itself does not fix this fixture. All eight cases and hashes are tracked in [the baseline data](tests/baselines/2026-10-09-house.json). The unchanged native renderer also passed all 960 floor and 5,184 general-occlusion pixel comparisons during this investigation.

The quoted “~4–7 ms” coverage saving has no verified measurement provenance here. At 10 FPS a frame costs about 100 ms; saving 7 ms would yield about 10.75 FPS, not by itself fix single-digit performance. A successful architectural change must remove materially more worst-case work or combine with other measured improvements.

## 2. Hardware and fidelity contract

Reuse the source-audited target in `../86Box/docs/86craft/1989-hardware.md`, with these qualifications:

| Component | Initial target | Validation required |
| --- | --- | --- |
| CPU/FPU | Intel 386DX/33 + Intel 80387 | No later CPU instructions; test integer and selected x87 kernels on the emulated 387. |
| RAM | 16 MB | This is the proposed PC baseline; do not silently use the board's 64 MB emulator ceiling. Test memory pressure separately. |
| Board | ASUS ISA-386C, proxy for Everex-class hardware | Not an exact Everex/cache model. Available firmware is later than 1989; strict firmware authenticity remains unresolved. |
| VGA | Video 7 VGA 1024i, 512 KB, provisional | Existing supported ROM date is 1990. Compare period card alternatives using actual framebuffer upload timings, not advertised SVGA resolution. |
| Disk | WD1007V-SE1 ESDI + CDC Wren V 94186-383 timing profile | Audit actual formatted CHS/BIOS/FAT partition capacity. The model's 383 MB name is unformatted, not an image-size specification. |
| Audio | Original Sound Blaster 1.0 / OPL2, 0x220, IRQ7, DMA1 | The earlier doc chose DSP1.03; current source labels that a prototype and defaults SB1 to 1.05. Audit retail/date evidence before freezing firmware; implement the conservative DSP1.x feature set. |
| Input | AT keyboard, COM1 serial mouse via DOS mouse driver | No assumption of PS/2 mouse or Towns game-port protocol. |
| Media | HDD installation; boot/install floppy; optional period CD later | CD is distribution/content, not live terrain. No assumed ATAPI or El Torito boot support. |

The checked-out emulator is `0579ccb9a`, based on upstream `f60098841542e2e4abca4197e7343dc356523716`; it contains the earlier hardware document, not a built/boot-tested PC VM. `DOScraft` was empty before this investigation. No usable DOS guest image, toolchain or 86Box executable was found under `86craft`.

Freeze an emulator revision, ROM hashes, exact config, disk geometry/timing, DOS/extender/driver versions and binary hash per release. Use the accurate 386 interpreter; the optional 486 interpreter is faster on the **host** but explicitly less accurate. Use period HDD timing, not RAM Disk. Verify the host sustains full emulation speed, otherwise separate guest-clock results from wall-clock responsiveness. x87 softfloat and PIT choices must be logged and consistent. [86Box machine settings](https://86box.readthedocs.io/en/latest/settings/machine.html), [disk settings](https://86box.readthedocs.io/en/latest/settings/hdd.html).

The historical hardware evidence already lives in the hardware document; this plan does not upgrade its provisional combinations into a claim of perfect 1989 authenticity. Modern development tools/algorithms are assumed acceptable for this hardware demake. Strict 1989 guest-software authenticity would require a separate runtime decision.

### Target decision: 386 baseline, optional 486 comparison

User decision, 2026-10-09: keep the Everex-class **386DX/33 + 80387** as the primary development and optimization target. The ASUS profile remains an emulator proxy, not an exact Everex motherboard/cache reconstruction. Keep a **486DX/25** configuration in reserve for compatibility and performance comparisons; it does not replace or raise the primary target.

Use the same 386-compatible DOS executable for the first comparison, built with `-march=i386` and without unconditional 486-only instructions. Intel's [architecture manual](https://www.intel.com/content/dam/support/us/en/documents/processors/pentium4/sb/25366821.pdf) documents upward binary compatibility. CPU compatibility is expected, but the complete DOS/extender, VGA, input, storage and audio configuration must still be boot-tested. The 486DX has an integrated x87 FPU; it does not require an external 80387.

For the initial A/B test, keep executable hash, DOS/extender versions, RAM, VGA card/memory, disk/controller/timing profile, seed, graphics settings and benchmark route identical wherever supported. Record unavoidable motherboard differences and emulator/cache-model limitations. Select a genuine emulated `i486dx` at 25 MHz on a compatible board: enabling the optional 486 interpreter on the 386 profile is **not** a 486 hardware comparison. Keep dynamic recompilation disabled and log FPU/PIT settings and emulation speed for both targets.

Measure the house fixture and each standardized walking/looking/editing phase separately, including render versus presentation costs and edit/streaming spikes. Report measured improvements rather than treating clock speed, manufacturer claims, or CPU-family labels as an FPS multiplier. A later 486-tuned executable, if useful, is a separate experiment and must not compromise the 386 build.

The secondary configuration is a reserved test target, not a supplied or verified VM. Do not label it an authentic, purchasable-in-1989 Compaq reconstruction: the Deskpro 486/25 was announced in November 1989, but a [contemporary availability report](https://www.computerwoche.de/article/2780242/mini-plaene-compaq-wird-pc-markt-zu-eng.html) gives January 1990. Exact board/firmware and retail availability remain to be audited before making a historical claim.

## 3. Runtime and platform split

### Default: DOS + DJGPP/CWSDPMI

Prefer a 32-bit protected-mode DOS executable using a pinned host-side DJGPP cross-toolchain and CWSDPMI. It fits the existing GNU C and GNU-style 386 assembly and provides ordinary HDD files, extended memory and access to PC devices. Build on the host; do not compile the game on the emulated 386. The bundled Zig freestanding compiler is useful for current tests, but it is **not** already a DJGPP DOS toolchain. [DJGPP project](https://www.delorie.com/djgpp/), [cross-compiler documentation](https://www.delorie.com/djgpp/v2faq/faq22_9.html).

Open Watcom plus a compatible 32-bit extender is a fallback if the cross-toolchain/setup is impractical or measured runtime overhead is excessive. It entails compiler/inline-assembly/ABI adaptation. A bare-metal PC kernel remains an escalation only after measuring a DOS bottleneck: otherwise it adds boot, FAT, memory, BIOS bridging, input, interrupt and storage driver maintenance before improving the renderer. [Open Watcom programmer's guide](https://open-watcom.github.io/open-watcom-v2-wikidocs/cpguide.html).

Use a minimal DOS boot with no unnecessary TSRs and no paging. Detect usable RAM, allocate fixed arenas at startup, lock interrupt code/data/stacks and fail clearly if the budget is unavailable. No allocator, DOS file operation or expensive game update in an ISR. PIT/keyboard/audio IRQ handlers only acknowledge and enqueue bounded work; preserve and restore vectors/hardware on normal exit and error paths. Profile protected/real-mode transitions. [DJGPP interrupt pitfalls](https://www.delorie.com/djgpp/v2faq/faq18_11.html).

### Code boundaries

| Existing area | Port treatment |
| --- | --- |
| Blocks/items/textures/font, inventory/crafting, collision/player rules | Reuse with type/header separation and regression tests; preserve behavior. |
| `common.h` includes Towns `hw.h` | Split portable fixed-width types/math from platform I/O; core must not include Towns ports. |
| `crt0.S`, `isr.S`, linker/boot ISO code, heap/sys layer | Replace with DOS startup/extender and PC platform implementation. |
| Keyboard/mouse/time | Map AT input to semantic actions while keeping the same bindings; full key-up state, extended keys, lost-event handling and mouse deltas. |
| `video.c`, hardware page flipping in `gfx.c` | Replace display/presentation; keep drawing/font/HUD behavior separately. |
| `render.c`, `raster.c`, `trap.S` | Port baseline behind a renderer interface; add independent candidates. Adapt object format/symbol decoration/ABI and test assembly against C. |
| Whole-world generation/cache, Towns HDD/floppy drivers and `save.c` | Replace storage/runtime ownership, retain useful codecs and edit/light algorithms behind new interfaces. |
| YM2612/RF5c68 sound | Replace with OPL2 and Sound Blaster mono PCM; preserve composition/effect identity as far as hardware permits. |
| Host/native test harness | Keep a software reference/headless mode. A host window or GPU must never do guest rendering for scored results. |

Copy a pinned Towns source baseline into the port or use explicit vendor imports with an import manifest. Avoid a live build dependency on whatever happens to be dirty in the Towns directory. Future fixes are cherry-picked/reviewed rather than silently inherited.

Suggested eventual layout: `src/core/`, `src/world/`, `src/render/{reference,painter,coverage}/`, `src/platform/dos/`, `tests/`, `tools/`, `profiles/`, `releases/`, and ignored `build/`/`runtime/`. The game belongs here, not inside the separate 86Box repository. Keep ROMs and mutable OS/world images out of Git; track configs, scripts, provenance and benchmark summaries.

## 4. Display and rendering experiments

### VGA backend first

1. Initial platform proof uses BIOS Mode13h (320x200, 256 colors), RAM rendering and measured framebuffer upload. This is a test pattern/renderer probe, not permission to shrink or redesign the game's HUD.
2. The first full game should preserve the Towns 320x240 layout (200-row view plus 40-row HUD) using a tested unchained VGA mode. Compare planar upload and page flipping against the simple backend. Do not presume either faster; arbitrary textured pixels differ from solid-fill/latch benchmarks.
3. Keep pitch/viewport/scale explicit rather than baking in Towns' 512-byte VRAM stride. Render in RAM so hidden-pixel work does not repeatedly cross the ISA video bus. Measure render, conversion/upload and retrace wait separately.
4. Investigate a half-resolution upload exploiting doubled pixels, plane masks and VGA vertical replication. Preserve HUD clarity and exact aspect/FOV; this is a candidate, not free bandwidth or an assumed default.

Mode X exposes VGA page-flip/plane opportunities but is not a drop-in linear framebuffer, as Abrash's own discussion emphasizes. Its publication date does not make it later hardware. [Abrash, Mode X](https://github.com/jagregory/abrash-black-book/blob/master/src/chapter-47.md).

### Competing renderers, in order

**R0 — portable painter baseline.** Same visible content, view distance, resolution, lighting and poses as Towns; correct display conversion and counters. This establishes the cost of the PC platform before an algorithm change. Preserve greedy merging, cached integer camera tables, early bounds tests and effective floor rejection.

**R1 — bounded hierarchical rejection.** Frustum-test subchunk bounds once; add conservative opaque occupancy/air-connectivity metadata updated only for edited chunks. Reject whole groups hidden by proven near surfaces before face projection. Doorways, cave openings, glass and unloaded neighbors must stay visible. Do not run a global visibility rebuild on each frame or block edit. Try cumulative coarse coverage as well as the current single-patch restriction, retaining depth bounds; partial tiles are never simply declared opaque.

**R2 — depth reference/contender.** Fixed-point reciprocal-depth buffer with span-level early rejection and texture sampling only after passing depth. At logical 160x100, a 16-bit buffer is 32,000 bytes and a coverage bitset 2,000 bytes; at 320x200 they are 128,000 and 8,000 bytes. These are just 3D buffers, excluding color/HUD/metadata and allocation rounding. RAM is available, but per-pixel arithmetic/read/compare/store on a 386 may cost more than the saved texturing. Derive quantization/range and stable shared-edge/tie rules; use higher precision in the correctness reference where needed.

**R3 — visibility-first opaque spans.** Traverse demonstrably front-to-back opaque terrain, subtract already covered row intervals or tile masks, shade only uncovered portions, and retain depth for objects that cannot use that ordering. Compare bounded row-span lists against bitsets/coarse full-tile masks. Span fragmentation has a fixed capacity and a correctness-preserving fallback, not memory growth or silently missing geometry. Generate depth while emitting visible spans if cheaper than a second pass. Do not just reverse a center-distance sort: overlapping/near-clipped merged faces and moving rotated models require an ordering proof or depth resolution.

**R4 — perspective/subdivision alternative.** If near split/setup cost dominates, compare axis-plane/projective incremental mapping with reciprocal correction at short span intervals against existing affine per-cell subdivision. Preserve perspective/FOV and texture identity. Test cracks, extreme pitch, near planes and thin features. Fewer polygons may justify extra span arithmetic, but must beat the 386 baseline.

Abrash reports that Quake emitted nearest spans to reduce overdraw and stabilize frame rate; this motivates the experiment, not a Quake-sized engine transplant or an FPS prediction. Static BSP/PVS assumptions do not transfer directly to a world the player edits continuously. [Abrash's CGDC rendering talk](https://www.gamers.org/dEngine/quake/papers/mikeab-cgdc.html).

A full 3D voxel DDA raycaster is a fallback research branch if the span pipeline fails: it avoids mesh processing but scales with pixels times traversed cells and complicates models/cutouts. A heightfield raycaster or Doom-style 2.5D world would lose caves, houses and vertical looking and is not an acceptable shortcut. A global precomputed BSP/PVS is likewise not the default for arbitrary building. No renderer is chosen on novelty alone.

### Transparency/ordering contract

- **Opaque:** may establish coverage only for pixels the raster rules actually fill. Bounding rectangles are not coverage. Holes, shared-edge cracks and near-plane gaps must not hide geometry.
- **Cutout:** zero samples do not write color, depth or coverage. Nonzero samples may write depth. Opaque terrain followed by depth-tested cutout/model passes is a straightforward correct starting point; it may leave some removable foliage overdraw, which is acceptable until measured.
- **Moving/intersecting entities and custom models:** depth-test against terrain and each other, or use a proven visibility ordering. A binary “terrain already covered here” mask alone cannot distinguish a model in front of a wall from one behind it.
- **True blending, if later introduced:** render ordered blended layers after opaque depth, testing depth but not treating partial alpha as full occlusion. This is not currently required and must not be added as an unrelated visual change.
- Glass/water cutouts behind glass, foliage, torches, rotated mobs, doors, camera-inside-water and block removal opening a sightline are required adversarial tests. Prove no stale coverage after edit, seam publication or camera movement. When mapping/rounding intentionally changes, use an independent high-precision reference plus reviewed golden images; pixel parity with the old painter alone is not a universal correctness oracle.

## 5. World, mesh and HDD redesign

### Bounded working set, independent of total world size

Use 16x16 horizontal columns with the existing height48 initially; chunks can remain 16 cubed for mesh/light locality. Separate authoritative block data, derived light, mesh, renderer visibility and save ownership. Maintain a coordinate-to-resident-slot index and fixed queues/pools; no per-frame global world scan.

One column's one-byte blocks plus one-byte light is 24 KiB. A 9x9 resident neighborhood costs 1.90 MiB, or 13x13 about 3.96 MiB, before meshes/heights/metadata. Prototype sizes by measured prefetch time and motion, not simply filling all RAM. A 16 MB target budget can tentatively reserve 4 MiB resident terrain, 2 MiB meshes, 1 MiB generation/light scratch, 1 MiB compressed cache/I/O and under1 MiB rendering/assets, leaving several MiB for DOS/extender, heaps and headroom. Measure actual free physical memory; this is a budget proposal, not an implemented allocator.

Keep global coordinates32-bit and draw/transform coordinates camera-relative. Current packed draw items use eight-bit absolute X/Z; store directories and mob visitation are fixed256 entries, and world allocation selects at most width256. These cannot be enlarged just by changing a constant. Audit ordering keys, squared-distance arithmetic, terrain noise tables, save lengths and overflow at coordinates255/256,1023/1024 and2047/2048. With FU4096,2048-block coordinates fit32-bit, but some products of distances do not; bounded local distances or explicit64-bit intermediates are needed.

Uncompressed terrain at height48 is 48 MiB for1024x1024 and192 MiB for2048x2048, **blocks alone**. Retaining all light doubles that. At2048, block+light would exceed a roughly383MB-unformatted drive before DOS, indexes, extra worlds or edits. Compression/on-demand creation is required; no promised compression ratio. World capacity is unrelated to how much geometry should be rendered locally.

### Durable region store

Use DOS HDD files grouped into moderate regions, an in-memory compact index and append-only changed-column/entity records with lengths, version/generator ID, sequence and checksums. Commit an index/journal record only after its data is written; recover the last valid record after interruption. Cache decoded neighboring columns and compressed ahead-of-player records; batch contiguous region reads and measure seek costs. Restrict formats to the chosen DOS filesystem and signed seek/API limits, split region files well below2GB, and handle disk-full/short I/O explicitly.

Derived meshes are disposable. Lighting can be recomputed or selectively cached; initial generation/import can bake it. Durable saves record player edits and deliberately persistent animals, not just a temporary disk scratch directory. Existing Towns HDD backing is explicitly temporary and is **not** an acceptable PC save format without redesign. Do not overwrite unrelated images or claim to save before reload/recovery tests pass.

DOS file calls are synchronous. A queued read is not automatically asynchronous, and a “2 ms budget” cannot interrupt a blocking seek. Start with multi-ring prefetch, large RAM slack and startup warming; log cold worst-case seeks. If that still produces frequent traversal hitches, evaluate a controller-specific asynchronous backend behind the same storage API. That is an explicit measured escalation, not a promise that background scheduling fixes blocking DOS I/O.

### Generation choices

Preferred path: random seed, deterministic **region-local** generation, ahead-of-player cooking/cache onto HDD and a warm startup neighborhood. Keep generator version immutable in an existing save. Coordinate-hashed features plus bounded feature halos produce the same trees/caves/ores regardless of visit order. The current world-wide RNG-driven cave worms and feature loops cannot be called independently per region and expected to match; either retain them in an offline cooker or redesign them with owner regions and bounded influence. Determinism tests cover reverse/random generation order and region borders.

Offer pre-cooked seed packs on CD/install media through the same store interface. Compare decompression+HDD read against CPU generation at cold and warm cache states. Full2048-world cooking before play is an optional measured mode, not a prerequisite: area is64 times a256 world, so a linear extrapolation can suggest cost but cannot establish a30–60second cooking time. The user's willingness to wait30–60seconds supports a substantial startup warm-up, not an assumed guarantee of cooking everything in that interval. No whole-world block scratch allocation.

### Mesh/edit scheduling

Keep greedy meshing and dirty-layer rebuilds initially. Separate urgent player-visible geometry from lower-priority lighting/new-column preparation. Publish coherent meshes atomically; an edited block must disappear or appear in the next rendered frame. Do not reintroduce the old multi-frame removal delay in pursuit of average FPS. First measure immediate local rebuilds. If expensive, prototype a bounded edit patch/overlay or smaller rebuild units, with seam and stale-face tests; do not simply leave a stale mesh visible.

Prefer fixed size-class/slab or per-chunk pages over a global compacting mesh pool if fragmentation spikes are measurable. Keep mesh/visibility versions per chunk plus relevant neighborhood signatures, so one distant publication does not unnecessarily invalidate every stationary renderer cache. Bound generation/light/mesh jobs by resumable units and CPU time, but separately account for uninterruptible disk calls. On cache reuse, cancel stale jobs and preserve unsaved edits before eviction.

### Mobs

Honor the stated design: ordinary distant hostiles despawn; preserve gameplay-critical live fuses/explosions until safe. Persist tagged/player-kept friendly animals and suspend their AI off-range. Untagged wildlife can be reconstructed by seeded regional spawning; do not add fence detection, taming UI or new rules without approval. A future ownership/tag signal allows preservation without saving every creature.

Audit current passive scarcity independently: source does a one-time seeded population attempt, rejects half of regions immediately, requires grass, and marks fully inspected empty regions visited. Readiness, slot limits, seed/biome and dormant ownership can affect encounters. This suggests test cases, not a proven diagnosis of the user's seed. Preserve initial behavior for port parity, instrument attempt/readiness/grass/cap reasons, then propose any spawning rule change separately.

## 6. Audio and coprocessor experiments

Sound Blaster1.x does not provide Towns' eight independent PCM playback channels. Start with original effects resampled to unsigned8-bit mono, a bounded software mixer and single-cycle DSP1.x DMA blocks; no assumed DSP2 auto-init, SB Pro stereo or SB16 commands. Choose a low initial sample rate and test gaps/latency while rendering slow frames and reading disk. Allocate physically suitable conventional-memory buffers that do not cross64KiB DMA boundaries; lock all IRQ data. The existing86Box source gates auto-init command0x1C on DSP2.0+. [Pinned DSP implementation](https://github.com/86Box/86Box/blob/f60098841542e2e4abca4197e7343dc356523716/src/sound/snd_sb_dsp.c), [DJGPP DMA guidance](https://www.delorie.com/djgpp/v2faq/faq18_13.html).

Adapt music to OPL2 instruments/sequencing; keep synthesized effects and sample generation off the frame hot path. Profile music only, effects only, combined and silent runs. Reissuing short single-cycle buffers and doing software mixing are real extra PC costs; shipping sound is not allowed to silently destroy the frame budget.

Keep integer/fixed-point transforms, lookup trig and reciprocal tables initially. Existing Towns builds disable x87 and have cheap additive camera transforms, so installing an80387 does not automatically help. Test batched transforms, projection or span perspective correction with actual conversion/memory costs. Compare fixed-point versus x87 output tolerances and guest timings with FPU on/off. No per-pixel floating-point rewrite without evidence, and no386 build containing486/Pentium/SSE instructions.

## 7. Standard benchmark and acceptance gates

Create a versioned generated test world and route format with checksummed geometry, seed, spawn, camera paths, edits and random state. Both renderer candidates consume identical content and input. Frame-count-independent routes support fair throughput comparison; add exact fixed-pose/command replay for pixel/work-count comparisons. Run each segment independently and together:

1. Open terrain walking; level looking; walking looking fully down and fully up.
2. Outside/inside a completed opaque house, roof/floor views, doorway/windows; then two-story and several dense adjacent houses.
3. A deliberately hidden dense world behind a close opaque wall, with no AI/edits. This exposes saved hidden work rather than just extra visible wall texturing.
4. Identical house with glass/water/foliage, open/closed doors, moving rotated mobs crossing the sightline and near-plane intersections.
5. Breaking/placing individual blocks, a rapid building sequence, torch removal/placement, chunk seams, and removal of an occluder while moving.
6. Cold/warm HDD traversal across boundaries, sudden turns/revisits, dirty eviction and disk-full/error/recovery.
7. Seeded live passive/hostile populations and protected-friendly unload/reload, with audio-on/off variants.

Record guest frame-time median/p95/p99/max, percentage over66.7/100ms, time from accepted edit to visible geometry, presented frame age, CPU stage times (game/light/generation/mesh/collect/cull/transform/clip/span/depth/texture/upload/wait), high-water RAM, queue debt, mesh/list overflow, projected faces, shaded/hidden samples, disk requests/bytes/latency and audio underruns. Count texture samples separately from actual nonzero writes and unique covered pixels. No modern host wall-clock loop timer substituted for386 guest performance, no RDTSC on a386, no FPS-only summary.

Separate initialization/cooking from traversal and steady-state house rendering; explicitly wait for needed meshes/jobs to finish in static tests. Make profiling optional and measure its overhead. Keep the existing correctness tests (floor960 views, distance1920 and general5184) as inherited coverage; add house, diagonal/edge, edit invalidation and randomized geometry tests against a depth reference.

Provisional performance goal: sustained at least15FPS in the agreed baseline house/walking suite with no routine single-digit sections, and no repeated streaming stalls over100ms. This is an acceptance target, **not a promised achievable result**; freeze view/resolution/workload before judging it. Next-presented-frame geometry for ordinary edits is a correctness requirement, while its wall-clock latency is still measured.

Use at least three repeats, report uncertainty and per-scene regressions. The chosen PC baseline (386DX/33,16MB, period video/disk) gets priority; historical Towns2MB/16MHz is diagnostic, not an imposed PC requirement. Do not accept a high-setting gain purchased with a clear baseline regression. A candidate neutral within measured noise on baseline and clearly better at high settings can stay, as requested. Disable or adapt extra visibility work outdoors only by reproducible scene/work criteria, not fragile “inside house” heuristics.

## 8. Milestones, deliverables and stop points

| Milestone | Deliverable | Completion gate |
| --- | --- | --- |
| M0: investigation/plan | This plan, reproducible house fixture and results | Findings distinguished from hypotheses; Towns untouched. |
| M1: reproducible PC platform | Pinned cross-toolchain, clean build, legal guest boot, config and launch script; test pattern/input/PIT/RAM/file roundtrip | POST/boot on exact profile; no paging; repeatable timing; exit restores DOS state. |
| M2: parity vertical slice | Small deterministic playable world and painter renderer; original bindings/inventory/HUD, full pitch and immediate edits | Screenshot/rules regressions; display upload profiled; no source-layout/ABI mistakes. |
| M3: house renderer competition | R1/R2/R3 prototypes and stage-level standardized A/B report | Correct transparency/models/holes; dense interiors improve without baseline losses. Pick winner or retain baseline. |
| M4: bounded large-world store | Durable region files, local generator/cooker, prefetch and edit-safe cache | 1024 then2048 traversal/revisit/restart, bounded RAM, no seams/lost edits, measured cold-disk stalls. |
| M5: full feature parity | Sound/music, crafting/furnace/containers, caves/ore/lighting, combat/mobs and existing menus | Audio/AI budgets and gameplay replay tests; explicit ownership policy, no unsolicited rule/UI changes. |
| M6: performance tuning/release | Selected VGA/span/FPU/cache tuning; runnable versioned guest package and benchmark manifest | Baseline gates, provenance, launch-from-any-directory test, retained known-good releases. |

M1 andM2 deliberately precede the large renderer rewrite; otherwise there is no trustworthy PC baseline. M3 can prototype world-interface changes, but M4 should not enlarge draw-distance implicitly. Each milestone gets a small coherent commit with its tests/results; renderer experiments stay separately selectable until accepted. No end-of-day pile of unrelated files.

No credible calendar estimate yet: DOS toolchain/ROM availability, framebuffer cost and renderer correctness are unmeasured. Estimate each remaining milestone after the first vertical slice and renderer comparison, rather than equating a shared386 ISA with a trivial port.

## 9. Release/repository safeguards and remaining choices

- Parent Townscraft repo tracks the DOScraft game/plan; the86Box fork remains independent. Emulator modifications are unnecessary initially and require separate rationale, tests and commits if introduced.
- Track manifests/scripts/source/test fixtures/small result summaries, not live world/OS images or downloaded ROMs. Immutable release packages include commit ID, binary/media hash and profile. Any mutable runtime image is per-release/copy-on-run, never shared concurrently by two emulators.
- A click launcher resolves paths from its own location, validates required emulator/ROM/guest files, uses only named image files, does not format/replace unknown disks, and leaves actionable errors visible. Test paths with spaces and invocation from another directory. It runs the selected manifest's build rather than whichever root ISO happens to exist.
- Preserve the Towns renderer/game and historical launchers. The pending TownsISO publication remains unrelated and uncommitted; this task does not finish or push it. GBA/SNES remain out of scope.
- Save compatibility is not assumed. Prefer a documented host-side import of supported Towns saves to the new region format; retain original saves untouched. Decide supported versions after roundtrip tests, not by loading raw Towns sectors as DOS files.
- Defaults can proceed without a blocker: DOS/DJGPP, existing gameplay/visual layout,16MB hardware target, HDD regions, random local generation plus optional cook/import, renderer A/B first. Ask before any required HUD/resolution redesign, passive-spawn rule change, ownership UI, larger view-distance default or strict1989 guest-software policy. Existing permission to replace backend structures is not permission to quietly alter gameplay.

## Reproduction

From the workspace root:

```powershell
python .\86craft\DOScraft\tools\audit_towns_house.py --ram 8
python .\86craft\DOScraft\tools\audit_towns_house.py --ram 2
```

Uses the existing bundled Towns Zig compiler, pyelftools and Tsugaru headless emulator. Raw hashes, frame summaries, guest memory dump and final screenshots live under ignored `build/house-*`; no production ISO is replaced. `HOUSE_PRESENT` changes only the test level and `HOUSE_OCC` toggles the already-existing culler in the generated fixture. It is a Towns renderer investigation, not an 86Box runtime or a shipped optimization.
