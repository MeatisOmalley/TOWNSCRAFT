# Master optimization retention audit — 2026-10-09

Compared historical master work from the first renderer `b3b88b48` on
September 30 through `e86b3e6f` (under `townscraft/src`)
with the current integrated source (under `fmtowns/townscraft/src`). A fresh
`git fetch origin` found no newer remote master than `35e73ffe`. This audit
checks source behavior, not just whether commits appear in ancestry.

## September 30 optimizations

| Master change | Current status |
|---|---|
| `bd64b78e`: assembly trapezoid/scanline setup replacing per-row span calls | Retained in `trap.S`, which is byte-identical to historical master. Far-mob quarter-rate updates and frozen menu backgrounds also remain. |
| `9d5e0fa4`: two-pixel unrolled texel loops, no-wrap fast path, aligned texture atlas | Retained; same assembly and atlas layout. Local pig artwork differs, not the terrain texture simplification or fast addressing. |
| `9d5e0fa4`: tabulated frustum planes, fully-inside plane skipping, 7-bit radix sort | Retained in `setup_camera`/`collect` and the sorting paths. |
| `9d5e0fa4`: direct VRAM page rendering instead of a full-frame copy | Retained and subsequently upgraded to triple buffering. `gfx.c` and `video.c` match historical master's latest versions. |
| `9d5e0fa4` / `b7cea21a`: cached HUD strip per page | Retained; `ui.c` matches historical master. |
| `b7cea21a`: direction/8×8-quadrant sub-chunk culling | Retained; group counts/bounds are produced by the adapted mesher and tested in `collect`. |
| `befff698`: base-spec view distance 8 | Retained in automatic quality selection. |
| `395ebd45`: 32-bit reciprocal-table texture gradient instead of 64-bit multiplies | Retained in the identical assembly path and `g_recip14` table. |
| `b5f56a5e`: reciprocal-table projection | Retained in `build_inv_table`/`project`, including the original far-depth fallback. |
| `f88ea455` / `c26425dc`: distant flat shading and textured/flat view presets | Retained; the original three presets remain, supplemented by local distance-mode presets. |
| `956e5046`: cached sorted terrain draw list plus separate dynamic entity list | Retained; cache validity also accounts for local floor occlusion. |
| `1d0694aa`: optional interlaced rendering | Not retained: deliberately removed in the prior integration to avoid stale alternating-row artifacts. Not restored by this change. PF8 now controls optional general occlusion. |
| `1f6d3626` / `56f599ec`: mesh streaming, bounded build budget and nearby-search reuse | Retained/adapted to cached terrain. Ordinary edit handling was subsequently improved by dirty-layer meshing; streaming remains. |

The old whole-world heap-sizing policy is superseded by cached-column memory
allocation, not copied unchanged. That is a storage-architecture replacement,
not a missing rendering optimization. No September 30 renderer speedup other
than the deliberately removed interlacing was found missing.

## October 8 follow-ups

| Master change | Current status |
|---|---|
| `dfccfbb5`: masked exposed-face scan and dirty-layer mesh rebuilding | Retained, adapted to cached column ownership. Ordinary edits rebuild changed layers immediately; streaming/seam work can remain bounded/background. |
| `bc26e2ec`: adaptive near subdivision and early off-screen face/model rejection | Retained. Adaptive remains the default; PF10 now additionally offers full per-cell near subdivision and no subdivision. |
| `1121d0a7`: additive camera and cloud tables | Retained. |
| `ea931665`: triple buffering | Retained; current `video.c` is byte-identical to historical master. |
| `e86b3e6f`: out-of-view mob-box rejection | Retained. Local floor/general occlusion supplements, rather than replaces, this test. |

No additional missing optimization was found in that master's optimization
sequence. Benchmark timing/counters remain test-only; they are not production
speedups. The proposed front-to-back coverage-buffer rewrite is not a landed
master optimization. It is still a separate renderer proposal, not restored here.
See `PERFORMANCE_RESULTS.md` and `MERGE_EVALUATION.md` for the earlier measured
integration decisions. This audit does not claim that streaming costs or the
house-rendering bottleneck have disappeared.

## Subdivision option

PF10 cycles Adaptive → Full → Fast/no split → Adaptive. Adaptive uses the
existing distance-dependent recursion unchanged. Full splits each nearby
merged surface into single cells; Fast bypasses that split. They use the same
near-surface trigger, painter ordering, meshes and rasterizer. Fast trades
more visible affine texture warping for less polygon work; Full reduces that
warping but costs more. This is not perspective-correct texturing and does
not change terrain or saves. Graphics settings are session-only.
Near per-cell subdivision existed in the first renderer on September 30;
`bc26e2ec` changed it to adaptive splitting on October 8. Full restores the
per-cell near-surface approach, not an entire historical renderer snapshot.

Switching modes also invalidates the optional occlusion proof map and its
cached hidden-item flags. Default behavior stays adaptive; no other control
or gameplay change is included. The existing help text is condensed by one
line solely to fit the PF10 hint in the unchanged panel.

## Deferred edit lighting fix

While streamed-column lighting owns the shared flood-fill queue, a player
edit publishes geometry immediately and queues its relight. Direct removal
or emission can change the edited cell's light without propagation changing
any neighbors. Previously that source change did not invalidate faces that
sampled it, leaving stale mesh brightness until another edit.

`relight_edit` now compares the source's complete skylight/block-light byte
before/after relighting and dirties affected neighboring face layers when
it changes. It uses the existing seam-aware invalidation and scheduler;
unchanged source light does not add work. The regression first failed on
the old source (torch light 14, adjacent mesh light 0), then passed after
the fix. It covers deferred torch addition/removal, all six adjacent faces,
column/layer boundaries and 2/4/8 MB cache profiles without a second edit.

Real-raster regressions additionally compare floor, distance and optional
general occlusion against their uncullled reference in all three subdivision
modes, including transparent floors/openings and entity boxes. A separate
fixed-pose mode-switch/wrap test checks proof invalidation and demonstrates
267/64/24 drawn faces (Full/Adaptive/Fast) in its near-floor fixture. Those
counts are reduced polygon work, not a measured whole-game FPS guarantee.

## Default-mode performance check

Three alternating before/after runs of the standardized six-phase level:
MODEL2, 16 MHz, 2 MB compatibility baseline, width 96, view 8, 160×100,
no mobs/HDD. Before is `74ab585a`; after is the lighting/subdivision source
with Adaptive selected. Both terrain hashes are `88493915`. FPS medians:

| Phase | Before | After |
|---|---:|---:|
| Look | 24.59 | 24.54 |
| Walk | 25.60 | 25.53 |
| Walk looking down | 19.57 | 19.58 |
| Walk looking up | 37.19 | 36.97 |
| Break/replace | 9.47 | 9.44 |
| Mixed | 25.60 | 25.54 |

All differences are within 0.6%, and run ranges overlap in every phase;
this does not establish a meaningful default-mode FPS change. It does not
measure Fast-mode gains or fix the house-rendering bottleneck. Raw runs
are retained under ignored `build/perf-audit/subdivision-default-{before,after}`,
`result-1.json` through `result-3.json`. Reproduce with `tools/perf_audit.py
--standard --width 96 --ram 2 --scale 2 --view 8`, the appropriate source
ref and distinct output names.

The untouched production image separately boots into advancing gameplay
at 2 MB without HDD and 8 MB with its dedicated 200 MB SCSI scratch disk;
the latter records 256 cooking writes, 25 starting-column reads and zero
storage errors. `tools/smoke_production.py --check-subdivision` verifies
Adaptive/Full/Fast/Adaptive using real PF10 press/release events, not guest
memory changes, on both profiles.
