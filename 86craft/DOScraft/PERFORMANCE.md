# DOS performance checkpoints

The user authorized optimization work on 2026-10-10, including independent
prioritization beyond the supplied list. This supersedes the old Phase B timing
restriction, **not** the unresolved full gameplay-parity acceptance. Audio is
still deliberately silent. No UI, quality, mob-rule or save-format changes are
authorized as incidental optimizations. The subsequent explicit request permits
grouping graphics options into Low/Medium/High and modest visual tradeoffs, with
adaptive subdivision in all three. Substantial visual/gameplay changes still
require approval before experimentation. See the preset table in README.md.

## Automated guest benchmark

Run `tools/benchmark.py --bios <known-good isa486.nvr> --label baseline` with
Python. The existing diagnostic CMOS seed is
`runtime/platform-486dx25-xecixfz2/nvr/isa486.nvr`. Dependencies must already be
installed. It builds separately in `build/bench`, creates a **new** private VM,
boots its ISO-extracted floppy, runs three fresh DOS processes, reads each
`C:\BENCHn.TXT` from the private HDD and closes only its own completed emulator.
No Windows keyboard, focus, capture, BIOS interaction or player save is needed.
A timeout leaves the test open for diagnosis rather than killing active I/O.

Benchmark-only builds now ignore live keyboard scan codes and mouse motion/
buttons. The interrupt handler still drains and acknowledges keyboard bytes;
normal gameplay and scripted gameplay smoke retain the real input backend.
A native test feeds all 256 byte values repeatedly and verifies no events or
held keys; existing keyboard/mouse parity tests also pass. A normal isolated
rebuild remains byte-identical (`4d2ef66520e55f19cbfc43f5b4fe5f65a57460ee5a3ffbd25609e8cda62b4bdb`).
This closes a diagnostic hole where host input could open inventory and pause
the supposedly unattended replay. The interrupted private run is not evidence.
New renderer comparisons use input-isolated baseline and candidate alike.

Results: `build/bench/486dx25/<label>/latest-report.txt` points to the complete
JSON, including compiler commands, executable/source/ROM/emulator hashes and
all raw samples. Guest PIT channel 2 measures microseconds; unchanged 100 Hz
IRQ0 resolves counter wraps. A native test covers wrap recovery and IRQ-phase
uncertainty. This uses neither host wall-time FPS nor unavailable 486 RDTSC.

Version 1 is a **frozen-simulation graphics/world-update replay**, not full
gameplay FPS. It uses procedural seed 4242, half-resolution view, eight-block
distance, inherited adaptive subdivision and occlusion-off. Six scenes each
have six warm frames then 32 samples: stationary, moving camera, moving full
down, moving full up, a 7x7 stone-house interior and alternating block edits.
Camera positions advance by frame, not elapsed time, for equivalent workloads.
Game physics/mob simulation is frozen; meshes/streaming/raycast/render/HUD and
real VGA upload remain live. Edit calls and initial house construction are
outside frame timing; resulting mesh/light work is inside. This limitation must
be fixed before ranking edit-latency/simulation changes. Native transport tests
and existing real-input gameplay smoke supplement, not replace, this replay.

Stage totals distinguish game/raycast, world updates, collection, entity sort,
page wait, sky, drawing, finishing/doubling, HUD and presentation. Presentation
also separates packing, ISA upload and retrace waits/timeouts. Last-frame VGA
readback and image hashing are outside timed frames. Three repeats expose
variance; compare each scene, p95/max and frame samples, not just aggregate FPS.
Readback proves all four VGA planes match RAM, not renderer visibility itself.

## FPS and slow-frame reporting

Reports and comparisons include average FPS, 1% low and 0.1% low, per repeat,
per scene pooled across repeats, and across the full measured replay. Average
FPS is measured frame count divided by total frame duration, not the average
of individual instantaneous FPS values. Each low is the reciprocal of the mean
duration of the slowest `ceil(N * fraction)` frames. This is distinct from the
also-reported nearest-rank p99/p99.9 frame-time percentiles.

Every FPS summary includes sample count, tail-frame count, whether there are
at least 100/1000 samples for the respective low, and whether its tail contains
at least ten frames. Short default runs (32 frames/scene/repeat, 96 pooled)
remain useful regression checks but their lows are effectively worst-frame
indicators, **not reliable tail statistics**. Existing evidence is not rewritten;
comparison tools can derive the added metrics from its retained raw frames.

Use `--frames 1000` for a longer screening run or `--frames 10000` for ten
0.1%-tail frames per scene per repeat. These runs can take hours on the emulated
CPU; the default host timeout increases automatically, or use `--timeout`.
Measured movement repeats the same bounded 32-pose route rather than drifting
into unrelated terrain. Long runs use wide PIT/stage accumulators and are
compared only with matching sample counts and movement policy. They remain
frozen-simulation replays: repeated routes warm caches, and combined metrics
exclude generation, warmup, inter-scene construction and report writing. Use
individual scene metrics to avoid hiding a building slowdown behind easy views.
The long-run plumbing requires guest acceptance; adding the metrics does not
claim a completed long-run benchmark or full-game 0.1% low measurement.
Verification: all 16 focused metric/parser/comparison tests pass, including
nonuniform frames, tail averaging, rounding, sample-support flags, long-run
units and mismatched-workload rejection. The real DJGPP i486 10,000-frame
benchmark links successfully; no long emulator run was started at this checkpoint.

## Initial independent priority

One preliminary run measured about 22 ms packing + 31 ms upload per frame;
retrace was 6–10 ms and **no 100 ms timeout fired**. Drawing was roughly 33 ms
stationary, 41 ms inside the house and 83 ms while looking straight down. These
are diagnostic replay results, not a claim to reproduce every player's scene.

1. Measure one-pass all-plane conversion against four strided framebuffer
   passes. Exact pixels, no gameplay/quality change; additional 57,600 B scratch.
2. Measure exact per-VGA-page unchanged-HUD upload avoidance. Existing HUD
   caching stops RAM drawing, but currently not packing/ISA writes. Comparison
   overhead might erase the gain: keep only measured successes.
3. Investigate draw/setup cost: exact camera setup reuse, remaining signed edge
   divisions, repeated clipping/subdivision setup. Existing shared transforms,
   reciprocal spans, greedy/cached meshes and assembly are already implemented.
4. Then assess conservative occlusion/front-to-back rewrite against house/down
   worst cases. It needs an independent visibility/order reference for cutouts
   and models, not just faster frames. Do not reduce visible content.

Atlas cache conflicts are another code/hardware-specific hypothesis, but
86Box cache fidelity needs checking before treating emulator timing as proof.
HDD/FPU/RAM changes are not automatically helpful: select measured bottlenecks.
Retain unsuccessful candidates as explicitly selectable experiment checkpoints;
never silently leave an unproven candidate in the default playable build.

## Retained first experiment

`--candidate pack-all` selects a single-pass, four-way unrolled converter;
`--candidate reference` keeps the original presenter. Neither changes gameplay
or quality. Its first guest attempt preserved at `runtime/game-play-12ipn3km`
failed the **diagnostic verifier**, not the image hash: the expanded scratch
buffer accidentally made `memcmp` read four planes against a one-plane readback
buffer. The verifier now explicitly compares 19,200 bytes. A native test runs
the actual presenter against an independent four-plane VGA model, checking
every pixel across 18 flips with odd pitches/unaligned source. The separate
converter tests check 448 frames and invalid-input guards. A fresh three-run
guest retest is required before adopting this candidate.

## Accepted optimization 1: all-plane packing

The corrected three-run test passed VGA readback and all six scene image hashes
matched the original baseline in every repeat. Packing fell from approximately
22.0 to 8.7 ms. The default full-game builder now selects this converter; the
benchmark retains `--candidate reference` as the untouched transport baseline.
No renderer, gameplay, resolution, view distance or graphics-option change.

| Replay scene | Original mean ms | One-pass mean ms | Frame-time reduction |
| --- | ---: | ---: | ---: |
| Stationary | 101.142 | 84.278 | 16.7% |
| Moving | 91.092 | 75.282 | 17.4% |
| Moving/down | 164.345 | 152.224 | 7.4% |
| Moving/up | 76.335 | 67.376 | 11.7% |
| House interior | 120.641 | 109.045 | 9.6% |
| Edit aftermath | 138.184 | 125.714 | 9.0% |

Raw samples, repeat means, p95, hashes and pinned hardware/binary evidence are
tracked in `tests/benchmarks/2026-10-10/pack-all.json`. Three repeats are strongly
consistent, but this frozen replay is not a promise of the same live-play FPS.
ISA upload remains roughly 31 ms; dense geometry remains expensive.

## Accepted optimization 2: exact unchanged HUD upload avoidance

Three further guest runs and the independent presenter model pass. A separate
shadow of each actual VGA page's HUD is compared byte-for-byte, using safe
unaligned word comparisons. Identical HUD plane bytes need not cross ISA again.
Changed bytes are copied/uploaded; titles, menus, loading/dimming, sharp text,
palette changes and all three pages remain correct. Cache initialization tracks
the VGA clear. This is not a probabilistic hash or gameplay dirty-flag shortcut.
Extra scratch: 38,400 B. Native tests also exercise the byte-only fallback.

Compared with accepted optimization 1, upload **including comparison overhead**
falls from about 30.7 to 27.1 ms. Stationary frames are neutral because retrace
absorbs the saving; moving improves 1.4%, down 3.1%, up 11.7%, house 4.8% and edit
aftermath 3.8%. No measured primary-scene regression. The default builder now
keeps both optimizations. `--candidate hud-cache` selects this checkpoint;
`--candidate pack-all` still tests the first change independently. Raw evidence
is `tests/benchmarks/2026-10-10/hud-cache.json`.

## Next independent candidate

VGA plane-map masks can broadcast one byte write to multiple planes. Exact
row comparisons can exploit doubled world pixels (and uniform sky) without
assuming all pixels match: outlines, HUD, text and full-resolution differences
must retain separate writes. Coalesced matching row runs may save more ISA
traffic than further CPU setup changes. Measure planning overhead, full-resolution
regressions and actual framebuffer equality before selecting it. This is a
system/code-specific priority, not simply following the supplied list.

The selectable `--candidate broadcast` experiment now implements exact per-row
pair/all-plane equality and coalesces runs using map masks 3, 12 or 15. It
includes sharp single-pixel differences rather than assuming a doubled picture.
Planning is disabled by the game-facing adapter when the existing full-resolution
option is active; that path retains optimization 2. Both paths have independent
native VGA-model coverage; the planner additionally has word/portable-path and
randomized byte-oracle tests. Extra persistent run scratch: 5,760 B.

The user also explicitly authorizes a 13h/chunky backend experiment if it shows
a major benefit, while retaining Mode X as an option. Standard 13h is 320x200;
our current display is 320x240 including its HUD. An isolated transport probe
may compare bandwidth/conversion, but cropping/rearranging the UI is not implied
permission. Also investigate ET4000 banked chunky 320x240 for a like-for-like
layout. Do not retire the working Mode X/reference path.

## Accepted optimization 3: exact plane broadcast

All three guest runs pass, with every scene's image hash matching both earlier
checkpoints and the original. `tests/benchmarks/2026-10-10/broadcast.json` retains
the incremental comparison against optimization 2. Upload plus planning falls
to about 15–19 ms. The default game builder enables broadcast for the doubled
view; existing full-resolution rendering takes the earlier exact upload path.

| Replay scene | Original mean ms | All three optimizations mean ms |
| --- | ---: | ---: |
| Stationary | 101.142 | 71.806 |
| Moving | 91.092 | 59.471 |
| Moving/down | 164.345 | 138.521 |
| Moving/up | 76.335 | 54.201 |
| House interior | 120.641 | 98.506 |
| Edit aftermath | 138.184 | 115.175 |

These are approximately 16–35% shorter replay frames, including an 18% house
improvement, not an assertion of identical live-play FPS. The architectural
house/down drawing problem remains. Next: expand timing to include real
simulation/edit-call latency, then profile raster setup/spans/overdraw and
compare chunky transport (keeping Mode X). The current replay deliberately
cannot rank AI or full block-edit latency. Existing fixed math, shared axis
transforms, greedy meshes, shade LUTs, division reciprocals and assembly are
retained rather than reimplemented or converted to x87 on speculation.

## Playable release checkpoint

The normal game executable (not the frozen benchmark) passed the automated
new-world and fresh-process reload tests. Both exercised actual guest AT input,
walking, full down/up look, torch placement/breaking, inventory/crafting
snapshots, VGA readback and the full-resolution toggle. Saving and reloading
returned success. Images were inspected; evidence is tracked in
`tests/baselines/2026-10-10-optimized-game/`. This is a smoke test, not exhaustive
gameplay acceptance or a live FPS measurement. All completed test VMs closed.
The host suite passed 184 tests (two dependency-dependent skips).

`tools/publish_game.py` published the smoke-verified normal executable into
`ISO/DOSCRAFT.ISO`. The existing `.cmd` validated successfully from `C:\Windows`,
without depending on its working directory. Player VM:
`runtime/game-play-ac_jv6sq`. Previous VM `runtime/game-play-hmcctnfd` remains
untouched, and its entire HDD was copied exactly into the new VM. The previous
ISO is preserved as `ISO/DOSCRAFT.before-471a1a8d4b54.ISO`.

Verified SHA-256 fingerprints:

- Normal executable: `a191bb04a312123d09c96e519062c84fd51b3cfe943059f021564df3dbabfc9f`
- Published ISO: `f48ffba45f14e774c2d96769fdca6e80390c2ba2f0aa89897263ff47e2bd9bfc`
- Previous ISO/backup: `4c837427b13597b903452b3b454b0a749334c26d85b7c1f2ed83f8c789f7fd08`
- Old and new player HDD: `8efe8c1f4ee321145c47db730832868bb59332a4d5ac10f935730a72fd3ffda5`

Generated ISOs and private HDDs remain local/ignored; source, launchers, tests,
benchmark evidence and smoke screenshots are committed. No gameplay/UI/layout,
graphics-quality or default-budget changes are included in this checkpoint.

## Follow-up experiments: keep evidence, not losing defaults

The existing optional single-patch occlusion prepass was tested independently
with all three accepted transport changes. Three repeats passed and the six
last-frame image hashes matched. It was slower: stationary 1.2%, walking 4.4%,
down 2.7%, house 2.1%, and edit aftermath 1.4%; up was neutral. Evidence:
`tests/benchmarks/2026-10-10/occlusion.json`. Occlusion remains off. This does not
rule out a front-to-back renderer: the existing prepass does not stop general
pixel overdraw or combine arbitrary opaque coverage into a complete proof.

The selectable `--candidate zero-dx` skips signed edge division when its
numerator is exactly zero. A native test compares the real original/candidate
rasterizers and real assembly across 24,000 polygons, both scales, winding,
clipping, cutout zeros, wrapping, flat spans, guards and occlusion queries.
Guest pixels also match in three repeats, but there is no meaningful repeatable
gain in house/down/edit scenes. Walking's third repeat contains a preserved
5.042-second outlier in presentation; its raw results are not discarded or
used as optimization evidence. The candidate is not enabled by default.
Evidence: `tests/benchmarks/2026-10-10/zero-dx.json`. A live FAT snapshot briefly
caught an in-progress directory/FAT update; the runner now retries such snapshots
without accepting corrupt/incomplete reports. The completed VM was collected
and only its identity-verified emulator process closed.

Priority shifts to geometry/setup elimination and ordered opaque coverage,
not more small arithmetic shortcuts. Cube terrain gives a useful ordering
structure, but merged face scheduling, cutout holes, models/mob parts and near
clipping must be proven rather than hidden by a blanket block-center sort.
The original painter remains the image reference and safe fallback. Introduce
an independent coverage/depth reference before enabling a larger renderer
rewrite, and test buildings/doorways, seams, moving models, transparent texels
and edits. Substantial changes to visible geometry or gameplay need approval.

The next-step design is `FRONT_TO_BACK.md`. Coverage tests are a correctness
scaffold, not enabled gameplay code or a measured speedup. Replay version 2
records `EDIT_CALLS` and call-plus-following-frame summaries. `--quality
low|medium|high` uses the normal game's adaptive preset table. Compare version 2
only with fresh version-2 baselines at the same profile; version-1 evidence
remains historical, not silently rewritten.

## Adaptive graphics-preset release acceptance

The normal game passed new-world and fresh-process reload smoke with real AT
input cycling Low → Medium → High → Low, including VGA readback at each profile.
All profiles retained adaptive subdivision; walking, full pitch, torch edits,
menus and save/reload also passed. Inspected snapshots and reports are tracked
in `tests/baselines/2026-10-10-graphics-presets/`. The host suite ran 201 tests,
passing with two dependency-dependent skips. Completed diagnostic VMs closed.

The preset ISO was published through the save-preserving workflow to the existing
launcher. Normal EXE SHA-256:
`4d2ef66520e55f19cbfc43f5b4fe5f65a57460ee5a3ffbd25609e8cda62b4bdb`.
ISO SHA-256: `2f5ab9c8ba1a2c0921c3c9a93cf8626de09ba2ce27c9c2e42529afb9b9fed9dd`.
Player VM `runtime/game-play-ns_83t_k` retained the byte-identical HDD hash
`8efe8c1f4ee321145c47db730832868bb59332a4d5ac10f935730a72fd3ffda5`.
Previous VM and `ISO/DOSCRAFT.before-a191bb04a312.ISO` remain available.
Launcher `-ValidateOnly` passed. No experimental occlusion, edge division,
camera reuse or front-to-back code is enabled in that release.

## Retained experiment: exact camera-table reuse, not a default

Reuse the camera/frustum axis tables only when all five camera coordinates,
view distance, world width and render scale match exactly. Initialization
invalidates reuse. Mesh edits, lighting and entities still update normally;
there is no approximate pose matching or change to fixed-point arithmetic.
The independent native test compares the actual original and candidate tables
over 12,000 poses, world widths, both scales, key changes and reinitialization.

Three fresh version-2 guest repeats per profile passed with all six scene image
hashes matching. Low stationary mean falls from 71.981 to 67.415 ms (6.3%);
High stationary falls from 152.788 to 152.262 ms (0.3%). Moving, down/up, house
and edit aftermath are effectively unchanged at both profiles. Retrace affects
the total-frame benefit; this is not a house-overdraw fix or a live FPS promise.
Raw comparisons and build provenance are retained in
`tests/benchmarks/2026-10-10/camera-low.json` and `camera-high.json`.
The user prioritizes moving/house gains over stationary-only improvements.
Consequently this candidate remains independently selectable, not enabled in
the normal builder or published ISO. The benchmark's `broadcast` candidate
remains the independent baseline.
Occlusion and zero-dx remain selectable losing experiments, not defaults.

## Retained experiment: compact logical raster

`--candidate packed-raster` draws clouds/geometry/outlines into a 16,000-byte
logical buffer for the doubled view, then expands once at finish. It preserves
the actual sky image, Mode X, sharp HUD and the full-resolution reference path.
Native comparisons exercise 24,000 polygons against the actual original assembly
with cutouts, wrap, clipping, colors including opaque zero, both scales, sky
bands/points, outlines, padding and guards. The portable renderer overlay is
explicit and provenance-recorded; no vendor changes are needed.

All three Low guest runs pass and all scene hashes match, but total frames are
slower in every scene: walking 59.120 → 72.118 ms, house 98.506 → 103.777 ms,
down 138.521 → 145.898 ms. House drawing itself falls about 41.0 → 38.1 ms,
but sky-copy and final expansion add roughly 11 ms before retrace effects.
The net result is a loss, not an optimization. Keep the selectable checkpoint
and `tests/benchmarks/2026-10-10/packed-raster-low.json`, not a normal-build
default. Stationary camera reuse also remains experimental, per user priority.

## Retained experiment: rejection before adaptive subdivision

`--candidate early-split-cull` rejects an invisible merged face before creating
its adaptive child faces, retaining all original per-child rejection. Visible
subdivision, UVs, lighting and gameplay are unchanged. Weighted fixed-point
frustum values are not necessarily monotonic between grid lines, so an endpoint
test alone is insufficient. The final gate checks included interior grid lines
only when the endpoint check suggests rejection. Native tests cover 120,000
actual parent cases against original child bounds at both scales, including a
negative control that demonstrates the endpoint-only hazard.

Three final Low guest runs match every image hash. Walking mean falls from
59.120 to 58.768 ms (0.6%), but house/down/edit means are effectively unchanged;
stationary changes by +0.2%. This is a small moving-view result, not a meaningful
dense-scene solution, and is not enabled by default. Evidence:
`tests/benchmarks/2026-10-10/early-split-cull-exact-low.json`. The earlier endpoint
prototype's raw recipe/results remain in `early-split-cull-endpoints-low.json`
for recovery, not as proof of general image correctness or an adopted variant.

Priority is exact front-to-back coverage that can skip complete hidden polygons
and covered scanlines BEFORE edge/gradient/texture work. The bounded capture
and actual-assembly image oracle are development scaffolding, not shipping
renderer integration or a measured speedup. Avoid spending further iterations
on stationary-only caches or sub-percent arithmetic tweaks.

## Front-to-back correctness checkpoint

The next architecture now has tested per-item primitive capture and exact
texture-state jumps across covered gaps, alongside the existing bounded
coverage bitset. Capture owns its projected vertices and refuses an overflowing
prefix; it is intended to reverse each item's actual emitted primitives while
the existing global painter merge is reversed with its exact tie rules.

The actual rasterizer/assembly serves as an independent image oracle for 48
overlapping forward-capture/reverse-first-opaque scenes. The gap kernel matches
20,480 actual assembly rows across all eight textured paths, every width 1–320,
clipping, wrapping, negative steps, byte rollover and cutout holes. DJGPP i486
object checks confirm no added divide/FPU/helper instructions in the gap kernel.
See `FRONT_TO_BACK.md` for the specific integration gates.

These modules are not connected to the playable renderer yet and have no guest
performance claim. The current ISO remains the tested adaptive-preset release;
none of the stationary reuse, compact raster, parent-culling or front-to-back
experiments is enabled. Rebuilding the normal default still produces the exact
same EXE hash `4d2ef66520e55f19cbfc43f5b4fe5f65a57460ee5a3ffbd25609e8cda62b4bdb`.
The final host suite ran 210 tests successfully with two dependency-dependent
skips. The pinned Towns import independently verified all 50 files unchanged;
the existing launcher validated from `C:\Windows`. All diagnostic VMs closed.

## Integrated coverage renderer: retained, not adopted

The exact reverse-painter renderer is now integrated in an opt-in diagnostic
build, with opaque ADD/ADC spans, cutout-aware coverage, conservative primitive
queries and original full-frame fallback. All 684 Low guest verification frames
match the original painter. Independent actual-raster/native-assembly tests
and the full 220-test host suite pass (two dependency skips). The Towns pin
still verifies all 50 files. See `FRONT_TO_BACK.md` and its raw reports.

Three input-isolated timed repeats nevertheless regress: walking 58.422 to
71.069 ms, down 137.999 to 166.984 ms, house 98.513 to 110.456 ms, edit aftermath
115.159 to 125.520 ms; up is neutral. Thus it is not a playable default. This
implementation's coverage/capture costs exceed the eliminated work; larger
scenes or a cheaper reverse traversal remain separate hypotheses, not gains.

## Independent candidate: exact edge reciprocal division

`--candidate edge-recip` changes only three edge divisions in the original
painter's `pedge_init`, plus one-time table initialization. It does not combine
with the losing coverage candidate or alter subdivision, UVs, material behavior,
clipping, resolution, view distance, UI or gameplay. A 32,772-byte reciprocal
table covers positive edge heights 1..8192. For bounded shifted magnitudes,
high-half unsigned multiplication followed by an exact correction reproduces
original signed division/truncation. Negative or oversized divisors/numerators
and other shifts retain original IDIV. No approximation or FPU conversion.

Native proofs cover 7,501,146 original x86 IDIV comparisons, all supported
divisors, both signs, boundaries and fallback inputs; 32,192 actual raster/
assembly polygon comparisons preserve pixels, guards, counters and occlusion.
DJGPP audits show the fast core is inline MULL/correction with no divide,
function/helper call, FPU or SIMD. Explicit source recipes replay exactly and
fail closed on upstream changes.

Three input-isolated Low guest repeats pass every image hash/readback but do
not improve performance: walking 58.422 -> 59.478 ms, looking down 137.999 ->
141.690 ms, house 98.513 -> 99.040 ms, stationary 67.421 -> 72.690 ms; up/edit
aftermath are neutral. Evidence: `tests/benchmarks/2026-10-10/edge-recip-low.json`.
This rejects the candidate as a default. A multiplication/table access plus
exact correction is not inherently cheaper on this target than the original
division. High is not used to excuse a Low regression. Preserve the selectable
implementation and tests; default flags and the playable ISO remain unchanged.

## Independent candidate: i486 word transpose packing

`--candidate pack-words` changes only RAM-to-planar conversion, keeping the
accepted HUD cache, exact repeated-pixel upload planner, Mode X timing,
three hidden pages, all 320x240 pixels and the original renderer. Its i386
kernel loads four adjacent source dwords, transposes the bytes using register
rotates/exchanges, and writes four plane dwords. The original byte packer
remains independently selectable. GNU alias/alignment annotations permit
unaligned buffers; byte-only C fallback is independently tested.

Native parity checks 25,900 frames across i386/x86-64 and default/portable
paths against an independent pixel reference, original all-plane packing and
plane extraction. Tests cover pitches, all offset pairs modulo 16, all short
capacities/pitches, null inputs and inaccessible-page exact boundaries. Actual
DJGPP i486 objects have 31 versus 42 inner-loop instructions per 16 pixels,
four versus sixteen source loads/stores, and no helper/FPU/SIMD/newer ISA.
Instruction counts alone do not establish a speedup; Low guest timing and
image/readback checks are the adoption gate. The normal executable rebuilt
byte-identically after both new experimental backends; the launcher validates
from `C:\Windows`. No playable ISO/default flag changes at this checkpoint.

Three Low guest repeats match all images/readbacks but regress: packing grows
from about 8.72 to 14.28 ms; walking 58.422 -> 64.924 ms, down 137.999 ->
143.270 ms, house 98.513 -> 101.675 ms, up 54.208 -> 57.369 ms. The actual
rotates/exchanges cost more on this target despite the lower instruction count.
Evidence is retained in `tests/benchmarks/2026-10-10/pack-words-low.json`.
Keep the experiment disabled; no High justification for a clear Low loss.

`--candidate pack-words-c` independently retains dword memory accesses but uses
C XOR/shift swaps instead of register exchanges. It additionally passes all
i386 native parity/boundary tests. The DJGPP loop has 51 instructions and 11
stack accesses per 16 pixels: register spills are a concern, not a demonstrated
improvement. Guest performance remains unmeasured; this is a recoverable probe.

## Independent candidate: release the frame register

`--candidate omit-frame` adds only `-fomit-frame-pointer` to portable C and DOS
adapter compilation. The existing DJGPP i486 `-O2` configuration explicitly
reports this option disabled. Freeing EBP is a register-allocation/codegen
experiment, not a renderer, world, floating-point, resolution or UI change.
All integer/i486/FPU-SIMD restrictions remain. Compiler commands and resulting
object hashes are retained in the benchmark's manifest; defaults remain unchanged
until measured acceptance and real-game smoke testing.

Target object checks confirm removal of the frame setup while retaining the
callee-save EBP push/pop ABI. Native presenter tests pass with the option.
IRQ entries save the complete register context and inspect an explicitly
identified interrupt frame through saved SS:ESP; they do not walk a C EBP
frame chain. Diagnostic C stack backtraces may be less informative with the
option, which is distinct from gameplay correctness. Low and High guest builds
are prepared, and the isolated Low comparison is underway.

Three Low guest repeats match all six hashes/readbacks: walking/up/static are
neutral, looking down 137.999 -> 136.422 ms (1.1%), house 98.513 -> 96.933 ms
(1.6%), edit aftermath 115.159 -> 112.525 ms (2.3%). Edit-call time is neutral;
mean call-plus-frame falls from 115.366 to 112.731 ms. P95 tails remain essentially
unchanged; do not characterize this as a hitch fix or a large FPS improvement.
Raw evidence: `tests/benchmarks/2026-10-10/omit-frame-low.json`.

Independent native codegen comparisons preserve all 24,000 original-raster/
assembly polygons. Explicit no-frame IRQ mock variants pass lifecycle, failure
unwind, keyboard, timer wrap and profiling/cadence checks. Real world/cache/
lighting/mob/save/cold-reload tests also pass with the compiler option and
preserve the complete save byte oracle. High timing and real-game smoke are
still required before adoption.

## Accepted optimization 4: frame-register codegen release

High also passes three input-isolated repeats, with unchanged scene hashes and
VGA readback. Mean improvements are small: walking 111.124 -> 110.598 ms (0.5%),
down 253.941 -> 252.361 ms (0.6%), up 67.383 -> 66.330 ms (1.6%), house
177.036 -> 175.983 ms (0.6%), edits 194.030 -> 193.154 ms (0.5%); static is
neutral. Evidence: `tests/benchmarks/2026-10-10/omit-frame-high.json`.
Together with the Low improvements above, this supports adoption without
reducing quality or changing gameplay. It does not fix worst-frame tails or
the architectural dense-building slowdown.

Normal-game smoke passes new-world and fresh-process reload, actual guest AT
movement, full down/up look, torch placement/breaking, inventory/crafting,
Low/Medium/High VGA readback and saving/loading. Evidence and inspected snapshots:
`tests/baselines/2026-10-10-omit-frame-game/`. The earlier complete host suite
passed 246 tests with two dependency skips; the release additionally checks the
exact accepted default flags. A fresh isolated build with the new defaults
reproduced the smoke-verified normal EXE byte-for-byte. Benchmark-only long-run
changes are excluded from this executable. All experimental renderers/culling/
packing variants and stationary camera reuse remain off. No graphics, UI,
mob policy, save format or FPU conversion accompanies this release.

Save-preserving publication updates the existing `ISO/Launch-DOScraft.cmd` path
to `runtime/game-play-zv5p5p7p`. The launcher validates from `C:\Windows`.
The previous `runtime/game-play-ns_83t_k` and its HDD remain untouched; the new
VM received its entire current HDD, including the user's intervening play/save
changes. The previous ISO is backed up as
`ISO/DOSCRAFT.before-4d2ef66520e5.ISO`. The embedded boot executable is checked
against the tested executable; media and HDD hashes are independently verified.

- Normal executable: `13803b3fa72128babd8be97c3b73a904e6891d2b6e80bfbd3d529764a8177a69`
- Published ISO: `edaa0c5c0a213effcbd283cf2f0b59452b5df9741708760d12e4c5c297c20686`
- Previous ISO/backup: `2f5ab9c8ba1a2c0921c3c9a93cf8626de09ba2ce27c9c2e42529afb9b9fed9dd`
- Preserved current player HDD: `c785fc4e3684c055d11c85928a8186d998a477ba4c122919ad09ba7ac98927b6`

Generated media/disks remain local and ignored; sources, tests, launchers and
release evidence are tracked. Audio remains deliberately deferred.

## Independent experiment: ET4000 banked chunky upload

`--candidate et4000-chunky` keeps all four accepted optimizations, adding only
an opt-in ET4000 CPU-access path when the repeated-pixel hint is off (High).
Low/Medium remain planar/broadcast. It gathers the existing pitched image into
consecutive bytes, temporarily enables CPU chain-4 and selects 64 KiB write
banks at 3CD, then restores bank zero/unchained access before the original page
flip or planar readback. Three 76,800-byte physical pages fit in 230,400 bytes.
The display stays 320x240 Mode X; HUD, palette, renderer and timing are unchanged.

The [Tseng ET4000 data book](https://manuals.plus/m/db23f00af4e83256d746dbcb9869926ed1e4e0dbde40e21a9b338e43038f9304)
documents linear-byte CPU access, segment pointers and chain-4 (sections 3.2,
4.2 and 4.4). The local emulator independently has bank selection and packed
chain-4 address handling, with a separate bulk-write fast path. That supports
the hypothesis; it does not prove hardware speed or physical display behavior.
No emulator source/configuration/timing patch is part of this experiment.

Exact per-destination-page HUD caches have explicit cross-path invalidation;
unchanged RAM shadows must not stand in for bytes written by the other path.
Native tests use an independent four-plane/banked model, all actual presenter
paths, 180 flips, unaligned source and both odd/standard pitches, all bank
boundaries, seven-flip path switching (every page crosses), full pixel checks,
unmodified other pages/unused VRAM and a deliberately corrupted readback.
An isolated normal rebuild is byte-identical to the published EXE. Guest Low/
High comparisons against a fresh current-codegen baseline remain required.
Do not enable this candidate or claim a gain from native speed/instruction counts.

## Finer attribution from existing original-painter samples

Read-only analysis of `broadcast-low-draw-profile.json` uses its exact matching
EXE (`99e2e43f30c9bcec7147dcc365b4c2601d408fc2bd766ab13a404fe34c9ec2b1`),
SHA-checked before disassembly. All three probes already satisfy the corrected
post-generation sampling/range gate. For each of eight original trap entry
points, identify the unique backwards pair-loop `JAE` and backwards row-loop
`JNE`; their targets delimit entry, row setup, texel loop, row advancement and
exit. Reject ambiguous/nonmonotonic boundaries rather than guessing.

Across 2,344 drawing samples: row setup 593 (25.3%), texel loops 677 (28.9%),
row advancement 94 (4.0%), entry/exit 7 (0.3%), other drawing 973 (41.5%).
These trap categories include opaque and cutout paths, not opaque alone.

| Scene | Drawing samples | Trap row setup | Trap texel loops |
| --- | ---: | ---: | ---: |
| Walking | 117 | 26 (22.2%) | 33 (28.2%) |
| Moving/down | 946 | 208 (22.0%) | 217 (22.9%) |
| House | 450 | 130 (28.9%) | 151 (33.6%) |
| Edit aftermath | 454 | 123 (27.1%) | 142 (31.3%) |

The original trap bodies are unchanged by the accepted C frame-register flag.
Nevertheless these are old-codegen, 100 Hz EIP samples including warm frames;
sampling/interrupt-boundary bias remains, not exact instruction cycles.
Setup deserves investigation alongside texel-loop/visibility improvements.
One invisible hypothesis is reusing row gradients only when their **exact**
rounded span width and shifted U/V deltas match; projected-row widths/UVs can
change, so a blanket once-per-face gradient changes images and is unacceptable.
Any cache checks, spills and code footprint must earn their cost on moving/
house/down scenes. This is prioritization evidence, not an implemented gain.

### Low gate failure retained before High testing

The fresh `codegen-current-low-v1` and `et4000-chunky-low-v1` runs both complete
three PASS repeats with VGA readback and essentially identical timings. However,
the down scene's RAM-image hashes differ consistently: baseline `cccac37c`,
candidate `e76ff760`. All five other final scene hashes match. The comparator
correctly refuses this pair; these timings cannot establish adoption. High VMs
are prepared but the queue stopped before launching them. The accepted game
and ISO remain unchanged, with the experimental compile flag off.

Preserved full raw/provenance reports: `et4000-low-mismatch-baseline.json` and
`et4000-low-mismatch-candidate.json` under `tests/benchmarks/2026-10-10/`.
The host suite passes 254 tests with two dependency skips. `--dump-frames`
now captures palette/indexed final images after the timing boundary to each
private HDD. Its timings are explicitly diagnostic and rejected by the
performance comparator, because added disk time can perturb later scheduling.
`bench_frame_report.py` checks completion and captured FNV hashes, compares
view/HUD/palette bytes, and renders unchanged frame dumps into a NEW directory.
Hash/region tests and diagnostic-timing rejection pass. This infrastructure
investigates the mismatch; it neither relaxes image gates nor changes gameplay.

### Image mismatch localization (checkpoint, not a fix)

Hash-checked disk dumps from all three fresh-process repeats give the same
result: only `walk_down` differs, by 20 physical pixels (five doubled logical
pixels), within x=8..31, y=54..77. All other scene pixels, the complete HUD and
all palette bytes match. Both VGA readbacks match their respective RAM image.
The first-repeat images and exact differences are retained in
`tests/baselines/2026-10-10-et4000-low-frame-diff/`; repeat 2/3 analyses remain
in the ignored private diagnostic outputs. This localizes a reproducible
world-image discrepancy, not an upload-only error or a demonstrated cause.

An independent native original-raster probe tests identical texture
neighborhoods relocated to different 64 KiB address phases, covering 24,000
polygons, both scales, clipping, subpixel edges, wrapping and cutout paths.
`tools/audit_atlas_relocation.py` passes with the original pinned C/assembly;
the address-phase hypothesis was **not reproduced**. This does not cover
every game-emitted polygon or rule out invalid texture coordinates, layout
sensitivity elsewhere or other scene state. No raster/game fix is inferred.
The tool preserves its source pin, compiler command, executable hash and
stdout in `build/audit/atlas-relocation/report.json`. It is a correctness
diagnostic, not 486 performance evidence.

The ET4000 candidate stays disabled and High testing remains held at this
gate. The published game/ISO and player's disks are unchanged. Continue
independent row-setup/early-rejection work rather than treating this backend
experiment as the only optimization avenue.

## Optional zero-gradient multiply probe

`--candidate trap-zero-gradient` adds a replayable assembly-only overlay to
the accepted transport/codegen baseline. When the original arithmetic's
**shifted** U or V delta is exactly zero, skip its multiply and final shift;
all nonzero cases keep the original operations. No UV precision, wrapping,
clipping or quality change. Each row pays two added tests/branches, so a
speedup is not assumed. Independently linked original/candidate trap objects
pass 24,000 full polygon images and 16,000 direct trapezoids, including signed
deltas, zero/nonzero quantized gradients, skip rows, both scales, clipping,
cutouts, buffer guards and final mutated Trap state. Guest performance is
unmeasured, the flag stays off, and this is checkpointed rather than adopted.

## Independent row-setup experiment: destination register reuse

`--candidate trap-destination` is separate from both ET4000 and zero-gradient
probes. Form the accepted row's destination in EDI immediately; the original
gradient and clipped-prestep code never touches EDI. Remove the later duplicate
destination setup and the `rowX` store/load. In the left-clipped branch, the
already-clamped row X is necessarily zero, so use the identical `0x8000`
pixel-center offset directly. No new branches, cache keys, stack slots, UV
arithmetic, texture addressing, visibility policy or quality changes.

Actual i486 DJGPP COFF disassembly confirms early destination setup and the
unchanged multiply/prestep/texel operations. The original 72-byte Trap ABI,
callee saves and eight entry points remain. Keep the unused four-byte `rowX`
BSS slot for now, avoiding an unrelated scratch-layout change. This removes
two/three instructions per visible scale-1/scale-2 row and two additional
instructions when left-clipped, but instruction counts are not a speed result.

Separately linked original/candidate assembly passes 24,000 full polygon
images and 16,000 direct trapezoids, comparing every framebuffer byte, guards,
pixel counters and final Trap state. Direct cases also vary shaded tile
offsets, signed UVs, zero/nonzero quantized gradients, widths, clipped/empty
rows and both scales. Native tests cover each probe independently. An automated
actual DJGPP object gate verifies smaller text, identical BSS size, all eight
exports/callee-save prologues and unchanged multiply counts with no FPU/SIMD.
Defaults and published ISO remain unchanged pending target measurements.

Low completes three PASS repeats with all six image hashes/readbacks matching:
house 96.932 -> 96.405 ms (0.5%), walking/down/static/edits effectively neutral,
up 54.033 -> 54.208 ms (0.3% slower; baseline repeat/retrace variability remains).
P95/worst tails are unchanged. Draw-stage means decrease slightly: down
81.914 -> 81.582 ms, house 40.713 -> 40.531 ms, edits 41.215 -> 41.030 ms.
Much of this saving is absorbed by presentation waits; it is not a house-hitch
fix or a broad FPS gain. Raw comparison, recipes, repeat/tail samples and hashes:
`tests/benchmarks/2026-10-10/trap-destination-low.json`. High is now running;
the Low result alone is insufficient justification to adopt the probe.

Experimental real-game smoke can now use `--build-output` with
`--trap-destination`, so testing a candidate need not overwrite the released
build's manifest or VM pointers. Default invocation is unchanged. Mocked
recipe/failure tests verify both selections and owned-process cleanup; they
are infrastructure tests, not actual guest acceptance. The complete host
suite passes 260 tests with two Windows symlink-permission skips; the two new
smoke-recipe tests pass separately. An isolated ordinary default build remains
byte-identical to the published executable after both trap overlays were added.
