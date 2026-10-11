# Group visibility feasibility: unchanged doorway house

Research only, 2026-10-10. Audited checkout: TOWNSCRAFT root `master`,
`e54e1e92` at inspection. No emulator, builds, renderer changes, fixture changes,
or commits were performed. The only sidecar output is this file. Low remains the
first screening workload; fused VGA work is outside this investigation.

## Recommendation

Proceed with a bounded, observational **group eligibility census**, not a new
coverage renderer. Existing groups are usable terrain batches and conservative
target bounds, but are neither rooms nor opaque surfaces. The existing collector
already performs the obvious group distance/frustum rejection. Moving the same
seam-safe wall predicate from items to these larger boxes is unlikely to solve
the house: its geometric proof becomes harder, while the retained item rejection
rate is already tiny.

There is one small independent implementation candidate worth counting first:
test the first quad's back-facing plane before doing the group's distance/plane
box work. This can save collection overhead, not newly hidden raster work.
A materially stronger wall/coverage batch experiment should be considered only
if the census finds substantial emitted work in wholly coverable groups after
the existing filters. No gain is predicted here.

## Retained evidence, with units and limits

Source: `tests/benchmarks/2026-10-10/wall-low-seam.json`,
`candidate.results[0..2].scenes[name=house]` and corresponding baseline scenes.
Headers specify version 2, frozen graphics replay, seed 4242, scale 2, view 8,
flat distance 0, adaptive subdivision (`subdiv=0`), occlusion off, and 32 measured
poses per repeat. These are historical independent wall comparisons, not timings
against today's combined production flags.

The following counts are **scene sums over 32 measured frames**, not per-frame
values. All three repeats have identical house counts and cone traces.

| House quantity | Baseline | Seam-safe wall candidate |
| --- | ---: | ---: |
| Collected item count summed across frames | 6,202 | 6,202 |
| Submitted raster faces summed across frames | 2,718 | 2,708 |
| Textured pixel/span counter summed across frames | 791,196 | 790,767 |
| Wall cone tests | disabled | 51,468 |
| Rejected terrain items | disabled | 13 |
| Frames with zero rejected items | n/a | 19/32 |

Candidate sources range from 4 to 12 per house frame, tests from 628 to 2,642,
and rejections from 0 to 1. Means: 1,608.375 tests/frame, 0.40625 rejected
items/frame, about 3,959 tests per rejected item. Only 10 raster submissions and
429 textured-counter pixels disappear per repeat (0.37% and 0.054% respectively).
The pixel counter is not unique visible area; items include models/entities and
are not synonymous with mesh quads or subdivision leaves. Thus `13/6202` is a
rough all-item fraction (0.21%), not a terrain-only eligibility rate.

First-repeat collection-stage time grows from 8.284 to 11.672 ms/frame and draw
from 40.713 to 49.886 ms/frame. The collection section includes camera/proof work
around collection, so its increase must not all be attributed to `collect()`.
Pooled house frame mean grows from 96.932 to 108.525 ms, about 12%. Static and
down also lose badly. For context, first-repeat down performs 162,091 tests and
rejects 450 items; edits perform 52,550 tests and reject 35. Those counts do not
make either candidate a measured win.

`wall-low-verify-v1-raw.json` retains the unsafe union-only diagnostic. Each house
repeat reports 51,034 tests and 68 rejected items; all three processes report
228 verified frames with zero mismatches (684 total across all scenes).
Its header explicitly says `WALL_VERIFICATION=TIMINGS_INVALID`; double-render
pixel/face counters are not comparable to single-render timing counts.
The native raster oracle nevertheless found case 11177's shared-row crack:
camera 25708/5211/25726, yaw 59, pitch 21, +Z plane at cell 104. Mathematical
union coverage is not raster opacity. Do not revive it as a batch proof.

Coverage is not intrinsically hopeless for house. In
`f2b-low-span.json`, first-repeat house raw counters record 1,276 hidden
primitives and 17 early-stop frames, with 505,473 sampled texels over 32 frames.
Nevertheless draw grows to 53.509 ms and total mean is 110.456 versus 98.513 ms.
The later `f2b-fast-low.json` result remains slightly slower in house (99.038
versus 98.513 ms) and substantially slower in static/walk. These are evidence of
occlusion opportunity and expensive implementation, not proof that existing
group boxes can exploit that opportunity. Their baselines differ from the wall
comparison. No retained report supplies group counts, group projected bounds,
group key ranges, or reasons for cone failure; these remain unmeasured.

## Actual batching, bounds, publication and order

The following are source facts. Locations refer to the inspected source files;
line numbers will shift if the parent task edits them.
`world.c`, `world.h` and `render.c` below mean `vendor/towns/src/` files;
`bench_runtime.c`, `f2b_raster.c` and `coverage_spans.c` mean files under
`src/platform/dos/`. Tool and test paths are relative to the DOScraft root.

| Location | Available mechanism and limitation |
| --- | --- |
| `vendor/towns/src/world.h:24-50` | A mesh quad is two words. There are 24 terrain groups: six directions times four X/Z starting quadrants. `group[0..23]` stores **counts**, not prefix offsets; `group[24]` is a separate model-cell count. Each nonempty group's `gbox` is three packed min/max cell bytes. Chunk size is 16, world height 48. |
| `world.c:870-931` | Groups use the quad's **origin** quadrant. Greedy extents can cross the 8-cell quadrant boundary. Bounds include full X/Z extents and the owning Y cell. Within a group, planes sort ascending for positive directions, descending for negative directions. There is no room/portal connectivity, opacity bitmap, key range, or group generation field. |
| `world.c:933-1021` | Partial publication merges retained and replacement layers in plane order. Old boxes are grown by new quads; deleted extents need not shrink. Loose boxes are conservative targets but reduce eligibility. They cannot establish solid wall coverage or doorway closure. |
| `world.c:1023-1047` | Allocation can trim groups from the end. `trimmed` flags pool allocation loss; counts reflect actual retained quads. Publication increments `g_meshVersion`, copies mesh words, then sets count/clean/meshed. Future sidecar metadata must describe the final retained publication after copying, not temporary scan buffers. |
| `world.c:441-454` | Mesher emission separately stops at `MAX_CHUNK_QUADS=2048`. Absence of `trimmed` does not prove every possible voxel boundary face was emitted. Actual retained quads can still be sources; inferred complete block surfaces cannot. |
| `world.c:150-155,1100-1106,1369-1399` | Edits mark affected cells/neighbors dirty. Dirty marking itself does not increment `g_meshVersion`. Block state and source readiness must be rechecked even if a mesh version did not change. |
| `world.c:1201-1232,1116-1129` | Geometry edits rebuild synchronously during dirty updates; light/stream work can defer or use sliced scans. Do not consume unfinished scratch meshes as proof. |
| `world.h:73-108,120-127`; `world.c:583-600,632-678` | Residency uses reusable slots; unknown `wget()` returns bedrock and `chunk_index()` can return the sentinel chunk. Check `world_column_sim_ready()` first. Unmesh clears counts and advances version. Pool compaction relocates `off`; never retain raw mesh pointers across world work. Identify groups by logical `(cx,cy,cz,grp)`, resolving residency/off again, with version and readiness validation. |
| `render.c:982-1007,1217-1251` | Smaller painter keys draw later. Horizontal faces receive end-of-slice keys. Side runs get end-of-row keys or become one Item per crossed cell depending on `nestX`. One mesh quad can therefore emit many Items. Group plane order is not global painter order. |
| `render.c:1009-1054,1679-1714` | `add_item()` silently stops at `maxItems` (3,000/8,000 depending on RAM). Radix sorting is stable and drawing traverses it backwards; equal-key entities draw before static Items. A group's emitted Items can be interleaved with other groups after sorting. A contiguous mesh range is not a contiguous painter range. |
| `render.c:1555-1600` | Camera tables refresh each frame, but the static list is reused within a camera cell and yaw/pitch differences of at most 8 angle units, subject to mesh/scale/view/floor invalidation. House yaw advances 16 units per pose (`bench_runtime.c:325-333`), so the measured house forces recollection each pose; other scenes and real play do not. |

Models (including doors/torches) are outside terrain groups. Entity parts are
another list. Model metadata can change without a terrain mesh publication
(`render.c:1717-1722`). Neither may be a proof source in the bounded first trial.

## Existing early rejection already does most obvious work

`render.c:1061-1128` rejects distant chunk columns and empty/floor-hidden/sealed
chunks, then rejects chunk spheres and carries only intersected frustum planes
forward. `render.c:1133-1180` uses each group's full bounds for distance rejection
and frustum minima; wholly inside planes disappear from the per-quad test list.
`render.c:1181-1215` stops at the first back-facing plane, then performs only the
needed quad distance/sphere tests. None of this requires projection.

This is already useful batch rejection before item emission/sorting and drawing.
Repeating it in a new visibility module would add cost. Its actual house hit rate
is not in the retained reports, so “most useful rejection” is a structural
observation, not a measured percentage. Groups mixing visible interior walls,
roof/floor and obscured terrain are plausible poor targets for whole-box
occlusion; the exact degree depends on chunk alignment and published extents.

Draw still performs actual face/leaf rejection in `render.c:538-577` before
projection, with adaptive recursion preceding the leaf gate. The retained exact
parent-frustum experiment (`tools/dos_sources.py:115-154`,
`PERFORMANCE.md:352-369`) was essentially neutral in house/down. Tighter geometric
frustum rejection alone has weak house evidence.

Do not simply tighten collector planes or use `box_outside()` in collection.
Its exact-pose gate is different from list reuse margins. Also weighted integer
plane tables can have interior extrema below endpoint minima. The prior parent
experiment explicitly confirms included grid lines on a potential rejection.
Any new whole-group descendant rejection must handle that hazard rather than
assuming ideal linear geometry proves actual fixed-point packet rejection.

## Concrete next experiment: observational GROUP census

This is a specification for subsequent authorized work, not code added here.
Use a DOS-only selectable overlay through the existing replayable recipe system
(`tools/dos_sources.py`); keep the pinned vendor import intact. Render the existing
unfiltered painter and leave the house/layout/doorway/poses and presets untouched.
No full-frame coverage/capture renderer is needed for this first decision.

1. At `collect()`'s nonempty-group entry (`render.c:1140`, before box decoding),
   count total groups/quads and whether the **first published quad** passes the
   existing visibility-bit test at `1187`. Because the published planes are
   ordered camera-facing first, failure implies no later quad in that group
   passes this back-face test. Count how many distance/plane operations could be
   avoided by moving this check ahead of them. Keep executing the baseline path
   and assert the group emits zero Items when predicted back-facing. Separate
   groups already rejected by distance/frustum from newly avoided box work.
   This independent candidate is a cheap read/decode/bit test plus `q=gend`;
   it needs neither wall sources nor raster proof. It adds a read on surviving
   groups, so eligibility and target timing can still reject it.
2. Count existing chunk/group distance/frustum rejections, group back-face breaks,
   quads actually visited, and **successfully emitted** Items. Histogram surviving
   groups by emitted Items: 0, 1, 2-3, 4-7, 8+. Record camera/list rebuilds and reuse
   separately. Increment in the existing paths, avoiding a second mesh scan.
3. Tag successful `add_item()` calls with a bounded diagnostic group ID and
   accumulate actual emitted `minKey/maxKey` and an enclosing target box per
   group. Preserve a logical chunk identity plus mesh version. Count models and
   dropped emit attempts separately. This handles run-to-Item expansion and
   sparse/loose groups without mistaking `ch->group[grp]` for saved draw calls.
   Sidecar overflow only disables the observation; it must not truncate drawing.
4. After ordinary collection/sorting, use the existing bounded wall-source
   selection (`tools/dos_wall_visibility.py:30-88`) against the intact list.
   **Observe**, once per nonempty emitted group, the seam-safe cone result for
   the enclosing target with `minKey`. Classify no-source, source-not-ready,
   near/extent failure, target straddles wall, order failure, coarse cone failure,
   and single-cell seam failure. Try a four-source cap and the existing 16-source
   ceiling as separate diagnostic settings, not item-by-source loops. Record
   pairs tested, successful groups and associated emitted Items.
5. As an explicitly unsafe opportunity estimate only, count how many groups pass
   the same coarse cone/order checks with the shared-cell guard omitted. This
   distinguishes broad-bound failure from the seam restriction. Never use this
   count to remove work or claim correctness. It is not an upper bound for every
   possible coverage design: different sources or unions could cover other groups.
6. Attribute baseline work to those groups using counters already entering
   `draw_quad()`/raster submission: descendant calls, submitted polygons and span
   pixels, distinguishing draw-time early rejects. This reveals whether a group
   merely saves collection bookkeeping or actually avoids expensive setup.
   Reset per frame; log compact totals after timing, not per item to disk.

Each measured pose should retain enough counts to compare house and edits without
hiding a bad route segment in pooled totals. Observational/profiling timings are
diagnostic only. A later independent candidate comparison must use matching
production flags and unchanged Low replay, without fused transport experiments.

Cheap decision gates (engineering screening choices, not established thresholds):

- If there are no safe whole-group hits, abandon the same-cone batch design.
  Do not subdivide groups or increase sources to rescue it without a separate
  demonstrated opportunity. An enclosing box plus a key lower bound is stricter
  than its member boxes under the same sources. The old 13-item house result
  therefore gives a strong pessimistic expectation, not a formal measured bound
  for a different source policy or differently sized emitted-item bounds.
- Only consider an active batch trial when groups save multiple emitted Items
  and real draw/setup work across meaningful house poses. Start with a minimum
  four emitted Items per candidate group and at most four sources; report the
  work missed by these chosen caps. Count source preparation, pair tests, bound
  handling and invalidation work against saved work. A single hit saving an
  already draw-culled Item does not justify a renderer.
- If only the unsafe coarse observation finds substantial multi-Item groups,
  the next problem is **raster coverage certification**, not a larger cone loop.
  If even that observation rarely passes, broad grouping/source choice is the
  immediate obstacle; avoid building a mask or reverse renderer for this design.
- Gate the first-plane optimization separately on the number of wholly
  back-facing groups and avoided box operations. It cannot be sold as an overdraw
  solution. Low neutral-or-better timing and pixel verification would be needed
  before spending High time. No such run was made in this sidecar.

## Conditional active experiment and conservative fallback contract

For any wall batch success, require a proof source to draw **after every**
removed target: `sourceMaxKey < targetMinKey`. Use actual emitted key ranges;
equal keys fail closed. Coplanar source unions use the maximum member key, as the
existing selector does. Do not sort groups by center, reverse the renderer, or
reorder models/entities. A group is only a rejection unit.

The smallest active same-cone trial would preserve the complete static list,
recheck sources and group proofs every frame, and test a cheap group-visible flag
at item dispatch. It avoids `draw_quad()` recursion, corner transforms,
projection, clipping and raster setup for rejected groups, but still pays original
collection/sort and item traversal. Instrumentation can determine whether this
limited saving is worth any active implementation. It does not require fragile
cross-frame removal from the cached list.

Skipping collection itself is a separate step: proof sources are currently
chosen **after** collection, so they cannot retroactively save its work. A source
prepass from publication would add traversal and must certify that each source
will survive facing/distance/frustum filtering, list capacity and painter order.
Retain the unfiltered list for restoration, or force immediate recollection when
any exact-pose proof invalidates. Removing groups and then reusing the old list
through permitted movement/rotation can lose geometry. Saving the entire house
collection section is not an available 8 ms optimization by declaration.

If coarse-only eligibility warrants stronger proof, a bounded exact raster source
prepass is a separate hypothesis: rasterize a few **actually emitted opaque
source items** into a write-coverage bitmap, preserving their original adaptive
leaves and actual raster spans, and query a conservative target-group envelope.
Use one map for sources all strictly later than the entire target group, or query
per source with its key; arbitrary unqualified key mixtures are unsafe. Source
cost is paid before target geometry, and saved groups would avoid their setup.
This is potentially more selective than one-cell cones, but unmeasured and may
repeat the losing coverage overhead. Do not build it until the census warrants it.
Existing relevant pieces: `f2b_raster.c:145-163` queries rectangles;
`coverage_spans.c` maintains actual span bits; neither authorizes filling a bbox.

Required guards for that or any active batch experiment:

- **Doorways/cutouts/seams:** `gbox` is only a target enclosure. Never fill its
  footprint as a source. Use actual opaque published quads/Items, excluding
  textures with zero samples (`render.c:148-158`) and all models/entities. Exact
  wall unions must preserve the one-block-wide, two-block-high opening defined in
  `bench_runtime.c:173-182`. Even an exact geometric union cannot erase the known
  raster seam. Retain the existing inset-single-cell guard, or certify actual
  raster-written pixels including internal row/subdivision gaps. Cutout holes
  contribute no claimed pixels. Block solidity/light opacity is not texture opacity.
- **Dirty/unloaded/trimmed:** require all source owners to be sim-ready, meshed,
  clean (`dirty==0`, `dirtyLayers==0`) and untrimmed each frame. Fail closed on
  unknown/sentinel/reused residency. Conservatively leave dirty/unready/trimmed
  target groups on the ordinary path too. Publish metadata only for final mesh
  contents; re-resolve offsets. Do not infer missing faces from block storage.
- **Near clipping/bounds:** sources currently require actual transformed corners
  at depth >=64 (`dos_wall_visibility.py:79-85`; renderer near depth is 12).
  A group whose bound crosses/approaches the near plane goes through normal
  drawing. Validate table indices and bounded 32-bit products; overflow/unsupported
  cases are visible fallbacks. For raster envelopes, eight ideal projected bbox
  corners are not yet a fixed-point descendant proof. Bound actual integer axis
  extrema and reciprocal quantization (`render.c:248-263`), clipping/raster
  rounding, and included subdivision grid lines, with outward rounding. Initially
  use actual emitted descendant packet bounds diagnostically to test a cheaper
  envelope; that diagnostic projection cannot count as avoided setup. Reject
  guard-clipped/unsupported bounds until independently validated.
- **Order/capacity/coherence:** exclude proof-source groups from rejection; every
  source must remain in the list and draw. Any list/source/sidecar overflow or
  uncertain key range disables the affected proof, or the full-frame optimization
  if uncertainty is global. Recompute/revalidate proofs on exact pose, mesh,
  scale/view/projection and source-readiness changes. A cached list is not a cached
  occlusion proof. Never leave stale hidden flags after source invalidation.
- **Verification:** ordinary painter fallback must be complete. Active group
  rejection needs independent actual-raster checks for doorway views, shared-cell
  case 11177, near/guard clipping, both scales, adaptive leaves, cutouts, edits,
  streaming/eviction, capacity and key ties. Every-frame comparison must rebuild
  an unfiltered reference list if collection was changed; drawing twice from the
  same already-culled list would not detect missing groups. Verification timings
  are invalid performance evidence. Preserve Low screening and do not manufacture
  eligibility by sealing the doorway or changing the camera route.

## Scope and remaining uncertainty

Facts above come from the inspected source and retained raw counters; no new
group eligibility measurements exist. Likely low whole-box eligibility, cheaper
first-plane collection, and selective raster-source prepasses are hypotheses.
The house coverage results show hidden work exists, but neither cheap certification
nor a useful group granularity has been established. The immediate deliverable
for a future experiment is the census and a go/no-go decision, not promised FPS.

Paths changed by this research task: `research/group_visibility/FEASIBILITY.md` only.
