# Local/master evaluation — 2026-10-08

## Integration completed after approval

Master's masked mesher, dirty layers and mesh-pool accounting now operate
with cached terrain. Player geometry is processed before background work,
including while a streaming-light job is pending. New meshes and full seam
refreshes still have bounded four-layer preparation; ordinary edits do not.
Outgoing cached-column seams now invalidate retained neighboring meshes.
Pool compaction packs first and grows reservations backward to avoid
overwriting unread source meshes. Bottom-chunk water sealing is refreshed
for geometry changes, not only top-layer changes.

The minimum-spec controlled removal takes 7.28 ms of mesh work in one
update versus 57.80 ms over four updates before integration. A corner
removal takes 9.44 ms in one update versus 173.72 ms over twelve updates.
Ordered mesh hashes match the original master and full-rebuild references.
Torch placement's lighting component is about 172 ms versus 159 ms before,
but its total lighting plus mesh work drops from about 477 to 209 ms.
Do not mistake the lighting component for an independently proven gain.

Validation: 1,932 randomized linear edits; 1,328 cached 2 MB edits; 462
cached 4 MB edits, each across 300 states; 3,200 exhaustive-stream search
states; cache/pool invariants; floppy paging and interrupted-save recovery;
active/dormant mob persistence; movement and keyboard tests; cave access;
and 960 downward-view pixel-equivalence cases. Native Windows rendering
tests adapt only assembly symbol names/ELF stack metadata in an ignored
build copy, retaining the real scan loops. Raw integration edit measurements
are in `tests/results/integrated-layer-edits-2026-10-08.json`.

Historical and standardized frame-performance evaluation follows this
integration. The sections below record the pre-integration decision.

## Decision

Do not retain local's entire `world.c` just because it is newer. Master has
the better dirty-edit mesher. Preserve the column cache for its independently
useful memory/storage behavior, but integrate master's dirty-layer tracking,
masked meshing and pool management into it. Do not send ordinary player edits
through the four-update background rebuild path.

Background scheduling is useful for bounding genuinely heavy preparation,
not as a substitute for reducing work. Keep that requirement separate from
the mesher. The present four-layer implementation is not automatically the
right final implementation: in the flat fixture even master's full single-
chunk rebuild costs less than a local background step. Reassess slicing of
new chunk builds after the faster cache-compatible mesher is measured on
complex terrain. This does not reopen the deferred landmark #12/#13 FPS
comparison.

This is an evaluated integration recommendation, not a claim that a hybrid
has already been implemented or proven. Production sources and ISOs have
not changed during this audit.

## What each mechanism actually buys

### Column cache: useful RAM headroom, not a demonstrated FPS optimization

Local keeps 25 terrain columns resident: 600 KiB of blocks plus light,
independent of map width. Master stores both arrays for the entire finite
map. At the same intended widths:

| RAM profile / width | Master's full block/light arrays | Local resident arrays + reserved compressed archive |
|---|---:|---:|
| 2 MB / 96 | 864 KiB | 728 KiB |
| 4 MB / 160 | 2,400 KiB | 984 KiB |
| 8 MB / 256 | 6,144 KiB | 1,624 KiB |

The second column is not the cache's complete cost. It also needs 24 KiB
of codec scratch, a 7 KiB static record directory, slot maps, heights,
chunk metadata and code. Different height/chunk allocations partly offset
that cost. Therefore the 2 MB saving is modest, roughly a tenth of a MiB,
not the 264 KiB suggested by comparing only resident cells. The saving at
4 MB and 8 MB is much more substantial.

The 4 MB instrumented runs retained width 160 on both architectures and
left about 0.33 MB versus 1.77 MB of high heap free; the 8 MB runs retained
width 256 and left about 0.59 MB versus 5.31 MB. These include instrumentation
and other source differences; they corroborate headroom, not an isolated
cache-speed result. The instrumented 2 MB master selected width 80, so its
headroom/FPS cannot be compared directly with local's width 96.

The cache does **not** currently expand the world or eliminate whole-map
generation. It also adds decoding, relighting, slot-aware accesses and,
after disk-backed saves, floppy paging. The local save/persistence work
uses its backing interfaces; reverting the whole world implementation would
require deliberately preserving/adapting those features, not silently
throwing them away. Headroom is a reason to keep the storage architecture,
not a reason to reject master's mesh optimization.

In the matched edit fixture, local synchronous cached meshing took 57.3 ms
for ordinary removal versus 39.7 ms using the same local mesher with linear
storage. That shows a real cost in the current cached access/update path;
it does not isolate every source of that overhead or prove every cache
access costs 44% more. The linear variant repurposes existing buffers for
the synthetic 80-wide fixture; it is not a shippable full-world build.

### Background rebuild: smaller individual stalls, delayed publication

With the existing local mesher, splitting an ordinary removal into four
updates lowers the largest update from 57.3 ms to 24.8 ms. Total update
work remains about 57.8 ms. This is useful smoothing, not meaningful work
elimination. The old complete mesh remains visible until all four pieces
finish. A corner edit affects three chunks, taking twelve updates to
refresh all of them. Earlier queued lighting/streaming work can delay an
edit further because the scheduler searches in coordinate order.

The existing column-cache regression expects immediate geometry rebuilds
and fails on the current production source:

```text
FAIL W=96 seed=4242: player geometry edits keep immediate rebuilds
```

The documentation says edits are immediate, but `world_update_dirty_chunks`
currently sends geometry through `mesh_step` whenever the cache is active.
This is an implementation regression, not an unavoidable consequence of
terrain caching. The same `world.c` exists in the local snapshot and the
selectively merged version: this delay was already in local work.

### Master's dirty layers: materially less work, immediate edits

Master records affected Y layers, preserves unaffected mesh data and uses
a masked mesher. Local marks whole chunks. The table contains median results
from three repetitions on the same synthetic scene, in emulated milliseconds
at MODEL2 / 16 MHz / 2 MB. These are mesh-update CPU times, not whole-frame
times or FPS:

| Alternative | Ordinary removal: total / largest update | Updates to finish | Chunk-corner removal: total / largest update | Updates to finish |
|---|---:|---:|---:|---:|
| Local, current cached background path | 57.80 / 24.80 ms | 4 | 173.72 / 24.88 ms | 12 |
| Local, cached synchronous test variant | 57.32 / 57.32 ms | 1 | 171.89 / 57.36 ms | 3 |
| Local mesher, linear-storage test variant | 39.74 / 39.74 ms | 1 | 119.27 / 39.79 ms | 3 |
| Master, force full chunk rebuilding | 22.24 / 22.24 ms | 1 | 66.44 / 66.44 ms | 1 |
| Master, actual dirty-layer rebuilding | 5.37 / 5.37 ms | 1 | 7.59 / 7.59 ms | 1 |

For ordinary removal, dirty layers alone reduce master's update work about
76% versus its full rebuild, independently of local/cache differences. The
full master mesher also beats the full local linear mesher in this fixture.
Thus there are two useful master improvements to preserve, not just a
different scheduling policy.

Torch placement still costs about 118 ms in master's `world_set` lighting
work and 159 ms locally, separately from rebuilding meshes. Layer meshing
does not fix that lighting cost. Sparse edits benefit more than broad
changes; master falls back to full rebuilding for heavily dirty or trimmed
chunks. Nothing here establishes a universal 10x game speedup.

## Correctness and comparison controls

- Source baselines: original master `e86b3e6f2e4a00942e0142605cdc5aca88382d22`
  and selective merge `159d95f26e46c3ec60322575e3c4a7ed272cd8d4`.
- All edit comparisons use an 80-wide flat fixture, identical blocks/light,
  the same 27 central published chunks and ten edit cases repeated three
  times. Normal generated terrain is replaced only in isolated test copies.
- All 270 measured edit states match their synchronous full-rebuild quad
  hashes, and hashes match across all nine configurations, including RAM
  profiles. This checks ordered quad content/counts, not every rendering
  property, disk behavior or every possible terrain shape.
- Master's existing randomized regression independently passes:
  `1932 edits, partial rebuilds match full rebuilds after 300 steps`.
  It checks canonical meshes, ordering, sealed flags and conservative bounds.
- Current integrated `edit_test.c` and `stream_test.c` still reference
  master's four-argument `rebuild_chunk`, absent from local's world source.
  Their presence in the merge is not evidence those integrated tests pass.
- Raw timings, memory layout, per-state hashes and test ISO hashes are in
  `tests/results/mesh-value-2026-10-08.json`.
- The preliminary generated-world FPS runs used different widths/scenes.
  They are excluded from the decision. No landmark #12/#13 conclusion is
  drawn here.

## Integration requirements before calling the merge clean

1. Retain deliberate local gameplay/input/save fixes; do not choose whole
   files based on which branch is newer.
2. Port master's layer masks and fast meshing with slot-aware neighbor access.
   Direct full-world neighbor offsets are unsafe at cached column seams.
   Reset layer/job ownership correctly when a slot is reused.
3. Give player geometry immediate priority; budget unrelated light/seam/new-
   chunk work independently. Do not retain the current delay as a tradeoff
   supposedly required by the cache.
4. Preserve master's pool fixes and validate trimming/eviction as well as
   ordinary meshes. Retain exact local save, dormant-mob and cache ownership
   behavior rather than reverting it incidentally.
5. Restore running randomized edit and stream regressions against the actual
   merged architecture; pass cache/save/mob/input tests. Then measure the
   combined implementation on the requested 2 MB / 16 MHz launch profile,
   including complex terrain and block edits during pending background work.
6. Commit those validated integration steps incrementally. Rebuild the final
   ISO from that committed source and record provenance; do not relabel a
   historical ISO as proof of source-level integration.

Master already had nearby-chunk mesh streaming. Local's column cache is a
second kind of streaming, for terrain/light storage. These are not the same
feature, and layer-based mesh updates are not fundamentally incompatible
with cached storage; the implementation needs a careful port.

## Reproduce the controlled experiment

Run from `fmtowns/townscraft` with the bundled Zig, Tsugaru and STUBROM present:

```powershell
python tools/perf_audit.py --name edit-master --ref e86b3e6f --mesh-probe
python tools/perf_audit.py --name edit-master-full --ref e86b3e6f --mesh-probe --full-mesh
python tools/perf_audit.py --name edit-cached --ref 159d95f2 --mesh-probe
python tools/perf_audit.py --name edit-cached-sync --ref 159d95f2 --mesh-probe --sync-mesh
python tools/perf_audit.py --name edit-linear --ref 159d95f2 --mesh-probe --linear-test
```

Repeat the unmodified master/local configurations with `--ram 4` or `--ram 8`
and distinct names for the memory-profile checks. Generated source copies,
ISOs, dumps and results stay under ignored `build/perf-audit/`. The timing
clock is the emulated 1 MHz counter with timer ticks resolving wraparound.
Update-call counts are not measured visible wall-clock latency: normal
rendering between calls would add time. All variants use the same compiler
(Zig 0.13.0, i386, `-O2`), not historical binaries built by different tools.
