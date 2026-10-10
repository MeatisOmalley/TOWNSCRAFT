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
