# DOScraft optimization priorities — 2026-10-11 (rev. 3)

A **ranked research backlog**, not a set of enabled changes or promised FPS gains. It draws on the DOScraft optimization conversations, the [DOS backlog](OPTIMIZATION_BACKLOG.md), [performance record](PERFORMANCE.md), [front-to-back experiment](FRONT_TO_BACK.md), [group-visibility audit](research/group_visibility/FEASIBILITY.md) and the Townscraft [retention audit](../../fmtowns/townscraft/OPTIMIZATION_AUDIT.md). Candidates are split into **invisible** changes (same picture and gameplay) and **gameplay/visual-affecting** changes, and each table is sorted by estimated FPS impact on **Low**.

## Current cost (published planar build, Low)

> **Hardware update (2026-10-11, later):** the target is now a 486DX/33 with an 11.1 MHz ISA bus, on an 86Box runtime that treats the ET4000AX as the 16-bit card it is. Low still 39.0 ms, walk 23.4, down 79.2, clouds 17.5, house 48.3 ms; upload ~5 ms instead of ~13. The table below is the earlier 25 MHz build; re-rank upload-cutting items (A2, A6, part of B2) against the smaller upload. [Details](PERFORMANCE.md)

486DX/25, 16 MiB, ET4000AX ISA, Mode X. Short three-repeat replay; the harness replays after its house/edit fixtures, so every scene carries leftover mesh/light work in "world update". [Report](tests/benchmarks/2026-10-11/low-planar-low.json)

| Scene | Frame | Draw | VGA upload | Pack | World update | Collect/sort | Sky |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| still | 58.2 ms | 30.3 | 13.1 | 1.8 | 7.4 | 2.9 | 1.8 |
| walk | 35.6 | 10.0 | 9.5 | 2.2 | 7.2 | 4.0 | 1.8 |
| looking down | 111.8 | 78.1 | 13.1 | 1.8 | 8.5 | 6.8 | 1.8 |
| clouds (walk_up) | 28.9 | 0.0 | 12.2 | 2.1 | 6.6 | 2.3 | 4.7 |
| house | 70.6 | 37.4 | 13.1 | 1.8 | 6.5 | 8.3 | 1.8 |
| edits | 76.6 | 37.7 | 13.1 | 1.8 | 12.1 | 8.3 | 1.8 |

**What the numbers say.**

- **Drawing dominates dense scenes, and it is setup-bound rather than texel-bound.** Still draws ~19,300 logical pixels in 30 ms. Average spans are ~10 pixels, so per-scanline gradient setup (two `imul` per row) and per-polygon edge setup (divides) cost about as much as texels. Looking down submits ~300 faces versus ~70 in the still view. The A3 probe below shows almost all of those are distinct visible surfaces, not subdivision fragments.
- **The ISA upload is a fixed ~13 ms floor.** Only fewer bytes can lower it.
- **World update and collect/sort add 6–20 ms.**

In the sampled draw profile, the texel loop is ~34%, row setup 15–26%, edge scanning 13–14% and projection/face setup 8–14%. [Sampler](tests/benchmarks/2026-10-11/current-low-draw-profile.json)

## A3 and A1 feasibility results (2026-10-11)

**A3: near-camera subdivision is too small a target.** A private Low replay disabled subdivision entirely, leaving the production build untouched. Against the matched three-repeat [Low baseline](tests/benchmarks/2026-10-11/low-planar-low.json), it removed only 3 faces/frame still, 0.75 walking, 4.25 looking down, and none in the house. Mean frame changes were −0.20, −0.20, −0.68 and −0.08 ms respectively; the up view changed −0.01 ms. These are diagnostic, visually different frames, and the −0.68 ms is **before** adding perspective-correct interpolation. The proposed 10–25 ms down-scene saving is ruled out. Retain geometric subdivision; do not spend a renderer rewrite on A3 alone. Private VM/report: `runtime/game-play-bwjervcg/performance.json`.

**A1: some merge opportunity exists, but painter order limits the cheap version.** A private instrumentation build captured the visible terrain items in painter order at four poses per scene (frames 0, 10, 21, 31 of the first measured run). The [census data](tests/benchmarks/2026-10-11/a1-census-4pose-low.json) gives these per-frame means:

| Scene | Visible terrain items | Consecutive, same material | Consecutive, any material | Plane-wide packing |
| --- | ---: | ---: | ---: | ---: |
| Still | 57.0 | 46.0 (−19%) | 44.0 (−23%) | 36.0 (−37%) |
| Walk | 19.0 | 16.2 (−14%) | 16.2 (−14%) | 16.0 (−16%) |
| Looking down | 278.8 | 267.5 (−4%) | 255.0 (−9%) | 182.0 (−35%) |
| Clouds/up | 0 | 0 | 0 | 0 |
| House | 74.5 | 72.5 (−3%) | 55.0 (−26%) | 42.2 (−43%) |
| Edits | 74.8 | 72.8 (−3%) | 55.8 (−25%) | 43.5 (−42%) |

The plane-wide figures are valid two-axis greedy rectangle partitions of visible coplanar unit cells, not a measured frame-time saving or a proven optimal packing. They ignore painter interleaving, near clipping, texture compositing, surface-cache maintenance and the extra cost of large affine polygons. The consecutive figures preserve the draw order but still assume rectangles can be merged without introducing occlusion errors. The census counts items/faces, not rasterized spans, so row-setup savings are still unknown. Same-material merging alone has only ~2 items/frame in the house and ~11 looking down; most of A1's remaining opportunity needs composite surfaces across texture/light boundaries. A bounded next experiment would prototype same-material order-preserving merging on a private mesh; it must measure net draw time and image correctness before trying a surface cache. Private VM/report: `runtime/game-play-jhclx4n4/performance.json`; its timings include diagnostic disk writes and must not be used for performance comparison.

**How to read the estimates.** "Est. Low saving" gives a frame-time saving for still / walk / house / down, from the stage costs above and the profile shares. These are unmeasured estimates unless marked **measured**. Gains overlap, so don't add rows together. The tables are sorted by the typical (mid-range) saving; confidence is stated separately.

## Done (measured, in the published ISO)

| Change | Measured Low effect | Notes |
| --- | --- | --- |
| **Non-blocking triple-buffered flip** with sub-tick PIT guard against the second most recent flip | still 67.4→58.2 ms (first guard); walk 41.9→38.2, walk_up 37.2→30.8 with the sub-tick guard; retrace wait 0 in every scene | Invisible; hashes unchanged. Only CRTC `0x0C` is written (page starts are multiples of 256). PC-speaker audio would need a different fine clock. [Guard report](tests/benchmarks/2026-10-11/flip-fine-v2-low.json) |
| **Producer-proven unchanged HUD** | −2.2 to −3.3 ms per scene | Invisible. [Smoke](runtime/hud-proof-release-smoke-20261011/report.json) |
| **Skip unread odd-row copies** (`raster_finish`) | still −1.3, walk −2.3 ms | Invisible; overlay bands still copied. [Comparison](tests/benchmarks/2026-10-11/raster-finish-skip-comparison.json) |
| **Planar Low/Medium** (`DOS_LOW_PLANAR`): scale-2 frames render straight into Mode X plane rows | still 62.1→58.2, walk 38.1→35.6, down 116.7→111.8, house 75.1→70.6 ms | Invisible; all hashes identical; pack 3.5→1.8 ms. Medium uses the same path (not separately benchmarked). [Report](tests/benchmarks/2026-10-11/low-planar-low.json) · [Smoke](build/smoke-planar-20261011/report.json) |
| **Very Low preset** (80×100 output, skipped texels) | ~9 ms below Low in dense scenes | Visual tier. Texel skipping now runs on separate entry points, so Low/Medium are unaffected (Low draw back to 32.3 ms). [Comparison](tests/benchmarks/2026-10-11/very-low-texels-v2-comparison.json) |

## A. Invisible changes (pending), by estimated FPS impact

| # | Candidate and first bounded experiment | Est. Low saving (still / walk / house / down) | Confidence | Main cost attacked; tradeoffs |
| ---: | --- | --- | --- | --- |
| **A1** | **Surface-cached plane merging.** Merge coplanar faces into large rectangles across textures and light levels. Four-pose census complete; next step is a same-material, order-preserving merge prototype and image/performance comparison. | Unmeasured; 3–19% same-material item reduction in visible terrain, 16–43% with unconstrained plane-wide packing | Low (speculative) | Per-polygon and per-row setup. Cross-material composites need cache maintenance; affine distortion and painter order are substantial constraints. The prior −8 / −2 / −10 / −15 ms estimate was not validated by the census. |
| **A2** | **Mid-frame CRTC scan-doubling for the view.** Show the 100 logical view rows at 4 scanlines each (max-scan-line 3); a PIT-timed write switches to 2 at the HUD boundary and restores during vblank. VGA memory then holds 100 view rows instead of 200. | −6.5 ms in every scene | Medium | Halves view upload (~13→~6.6 ms). The failure mode is a visible glitch band when the interrupt is late (real-mode disk I/O, SB IRQs). Needs PIT-to-retrace resync and a check that 86Box handles mid-frame CRTC writes faithfully. |
| **A4** | **Collect/sort invalidation census, then cut re-collection** (old #8). Count which condition throws the coherent face list away (cell change, >2.8° turn, global `g_meshVersion`, floor occlusion), then target it: per-chunk re-collection, holding back out-of-view mesh publication, or wider angular margins. | −1 / −2 / −6 / −4 ms | Medium | Collect/sort 2.9–8.3 ms. Painter order must still come from the current camera cell. [Collect cache](vendor/towns/src/render.c) |
| **A5** | **Measure and streamline world update** (old #15). Split mesh build, lighting, publication, streaming and disk time; target the dominant step. The replay shows ~7 ms per frame of mesh/light work after edits even in a still view. | −3 / −3 / −2 / −3 ms (more on edit tails) | Medium | World update 6.5–12 ms. Preserve immediate edits, light correctness and saves. [World](vendor/towns/src/world.c) |
| **A6** | **Exact destination-page dirty uploads** (old #7). Compare new packed view rows with the VGA page about to be written; upload only changed rows/segments. Count bytes and calls. | −6 / ~0 / −2 / ~0 ms | Medium | ISA upload in stationary views only; overlaps A2. Must handle page cycling, HUD, menus and overlays. |
| **A7** | **Vertical merging of side faces** (old #13). Walls are always one block tall today (`runs1d`/`runs1d_z`). Census same-key vertical neighbors, then merge, with matching vertical near-camera subdivision. | −2 / −0.5 / −3 / −2 ms | Medium | Per-polygon cost only; row count unchanged. Partial y-layer rebuilds must invalidate merged runs. Largely subsumed by A1 if that lands. [Meshing](vendor/towns/src/world.c) |
| **A8** | **Fast setup for ordinary unclipped terrain quads** (old #12). Bypass general convex-polygon edge chains for the proven 4-vertex case. | −2 / −0.5 / −2 / −4 ms | Medium | `poly_scan` setup. Keep clipping/model/cutout fallbacks. |
| **A9** | **Opaque-span assembly specialization** (old #11). Count eligible texels for constant-U/V, long unclipped, no-wrap spans; add one compact kernel at a time. | −1 / −0.3 / −1.5 / −3 ms | Low | Texel loop; code size can evict the 8 KiB cache. |
| **A10** | **Cache-aware raster/data layout** (old #20). | −0.5 to −1.5 ms | Low | Easy wins (aligned atlas, tables, assembly) are already taken. |
| **A11** | **Group eligibility census, then selective occlusion** (old #23). | ~−0.5 ms; situational | Low | Previous coverage/wall-cone prototypes lost. [Audit](research/group_visibility/FEASIBILITY.md) |
| **A3** | **Perspective-correct spans instead of geometric subdivision.** Full subdivision removal measured as a private bound; defer this renderer rewrite. | At most ~0.2 / 0.2 / 0.1 / 0.7 ms before perspective-correction cost | High (small bound) | Only ~4.25 down-scene faces/frame came from subdivision. New UV divisions would consume some or all of the measured saving. |
| **A12** | **Sealed-room/portal connectivity proof** (old #24). | Large only in qualifying sealed interiors | Low | Big correctness burden; the doorway house may not qualify. [Research](research/sealed_room/README.md) |
| **A13** | **Mode 13h/packed or direct-VRAM presentation experiment** (old #22). | ~0 expected | Low | Mode 13h crops the 320×240 UI; direct VGA drawing trades one upload for many slow writes. |

## B. Gameplay- or visual-affecting changes (pending), by estimated FPS impact

| # | Candidate and first bounded experiment | Est. Low saving (still / walk / house / down) | Confidence | What the player sees; notes |
| ---: | --- | --- | --- | --- |
| **B1** | **Alternate-row (interlaced) rendering** (old #9). Render half of the logical view rows each frame, alternating, and keep the other half from the previous frame. | −12 / −4 / −15 / −30 ms | Medium | Halves row setup and texels. Combing/shimmer on motion; Townscraft removed its interlacing for exactly that. Upload only shrinks if the stale field can stay resident across the three VGA pages, which needs page bookkeeping. |
| **B2** | **Smaller live 3D view panel** (requested 2026-10-11). Doom-style screen-size option: render the 3D view in a centered window, e.g. 256×160 (128×80 logical) or 224×140 (112×70), same FOV, with a static border and the HUD unchanged. | 256×160: −12 / −7 / −13 / −21 ms. 224×140: −16 / −9 / −18 / −29 ms | Medium–high | Cuts drawn pixels (−36% / −51%), rows (−20% / −30%), view upload (13.1→~8.4 / ~6.4 ms), pack and sky together. Polygon count is unchanged at the same FOV; cropping the FOV instead saves more but gives tunnel vision. Presenter needs a window row range, a cached border and repositioned overlay bands. UI layout change. |
| **B3** | **"Potato" 160×50 view (2×4 pixels)**, a tier below Very Low. | −10 / −3 / −13 / −26 ms | Medium | Halves rows and texels; upload unchanged unless combined with A2 (8 scanlines per row would quarter it). Very chunky vertically. |
| **B5** | **Shorter Low view distance** (8→6 blocks, with fog). | −6 / −2 / −3 / −2 ms | Medium | Removes many tiny distant faces, which are setup-heavy. Clear draw-distance change. |
| **B6** | **Flat-shade distant or small Low faces** (old #6). A texture-to-flat distance threshold (Low `flat_distance=0` today) and/or a larger `g_flatLOD` (currently 6 render pixels). | −5 / −1 / −5 / −3 ms | Medium | Skips texel work and per-row gradient multiplies for those faces. Visible texture loss in the distance. [Preset](src/platform/dos/graphics_presets.c) |
| **B7** | **Approximate UV-gradient reuse over 2–4 scanlines** (old #10). | −3 / −1 / −3 / −5 ms | Medium | Row setup; slight shimmer. Different from the neutral exact gradient-entry probe. |
| **B8** | **Distant-terrain impostors.** Render far chunks (5–8 blocks) into view-facing textured panels in true 3D and re-render a panel when the view angle to it changes beyond a threshold. Arbitrary pitch is fine. | −2 to −6 ms in open scenes | Low (speculative) | Replaces many tiny far faces with one quad each. Parallax error grows quickly at Low's short view distance; probably more useful for extending Medium/High view distance cheaply. |
| **B9** | **8×8 Low textures plus a compact shaded atlas** (old #19). | −0.5 to −1.5 ms | Low | Texture cache only; the same number of VGA writes. |
| **B4** | **Low subdivision off/limited.** Disable or cap `draw_quad_split` on Low. | Measured full-off bound: −0.2 / −0.2 / −0.1 / −0.7 ms | High | Visibly more affine warp for a small gain. Do not advance. |
| **B10** | **Camera-facing billboards for flowers/grass/torch flames** (one quad instead of two double-sided cross quads). | Small; vegetated scenes only | Medium | Slight look change when circling a plant. |
| **B11** | **Voxel ray/slab renderer** (old #14, rethought). A column renderer needs y-shearing for pitch, which can't look straight down, so it is **ruled out**. The only full-orientation variant is Voxlap-style casting along screen lines that radiate from the vertical vanishing point, with a resampling pass to the screen. | Unknown; could be large in dense scenes or a loss | Very low | Weeks of work. Leaves, glass and water break single-pass front-to-back. Doors, beds, torches, crosses and mobs still need polygons clipped against a depth buffer. Radial resampling adds per-pixel cost and blur near the vanishing point. Do not start without a host-side cost model. |
| **B12** | **Mob count / entity LOD cap on Low.** | Unmeasured | — | The replay freezes simulation and has ~0 entity cost. Requires a live-simulation profile first. |

## Hardware and emulator experiments (not game changes)

| Experiment | Expected effect | Notes |
| --- | --- | --- |
| **Period-correct ISA bus/wait-state A/B, same ET4000** (old #16) | Up to ~−6 ms of upload; unproven | Private VM only; the game is unchanged. |
| **Period-correct VGA-card A/B** (old #21) | Low expected | The ET4000AX is already strong; a different card endangers the High banked path. [86Box ET4000](../86Box/src/video/vid_et4000.c) |

## Already done or rejected — don't retry unchanged

The release has affine 16.16 texturing, reciprocal tables, greedy/exposed-face meshes, frustum/backface rejection, additive camera tables, adaptive subdivision, four-texel assembly loops, producer-proven repeated-row packing and broadcast, per-page HUD caching, ET4000 banked High upload with direct pitched rows, word-wise HUD comparison, async page flips, HUD proof, odd-row skip and planar Low/Medium.

Measured losing or neutral checkpoints include:
- exact front-to-back coverage (two variants)
- wall-cone item rejection
- the old coarse occlusion prepass
- full-frame compact-buffer expansion (distinct from the shipped planar path, which never expands in play)
- exact edge-reciprocal substitution
- word transpose
- fused Low pack/classification (two variants)
- shared-corner projection reuse
- exact-zero/stable gradient setup
- depth-aware 2×2 leaves
- first-plane group facing

Several were measured while frames were rounded to whole refreshes. Re-measure a neutral one before writing it off for good. [Measured history](PERFORMANCE.md) · [Coverage experiment](FRONT_TO_BACK.md)

## Test order and decision rules

1. **Completed:** A3/B4 subdivision-off Low sweep and A1 four-pose terrain-item census. A3/B4 are too small to pursue. B5 and B6 remain untested and visibly change Low.
2. **Invisible upload win:** prototype A2 in a private VM; verify no glitch band under disk streaming and SB audio.
3. **B2 smaller panel:** prototype at 256×160 and 224×140 with the same FOV; measure frame, draw, upload, and moving/doorway images.
4. **A1 next gate:** prototype the same-material, order-preserving adjacent merge on private geometry, then compare image hashes/readback and draw time. Plane-wide cross-material surface caching remains speculative until a smaller test demonstrates useful net saving.
5. **A4/A5:** profile first (invalidation reasons; world-update breakdown, including p95/max and edit-to-visible latency).
6. **Every candidate:** use the current production flags, fixed seed/route, three repeats and Low-first scoring. Compare frame, draw, pack, upload, world update, byte counts and visual evidence. Keep the doorway route and High reference; test save/reload and VGA readback for any adopted presenter change. Gameplay/visual rows need an explicit go-ahead before they ship.
