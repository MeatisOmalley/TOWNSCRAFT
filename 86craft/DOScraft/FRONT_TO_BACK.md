# Exact front-to-back experiment

This is an integrated, opt-in renderer experiment, not a shipping optimization
or an FPS claim. The playable build keeps the accepted painter and Mode X.
Substantial visual/gameplay changes require prior approval. All graphics presets
use adaptive subdivision; reduced subdivision is not this experiment.

## Why this is next

After transport optimization, the Low replay spends about 41 ms drawing inside
its house and 83 ms looking down. It samples roughly 24,725 and 30,392 textured
logical pixels respectively, against a 16,000-pixel viewport. This counter
excludes flat spans and includes transparent samples, so it is not an exact
overdraw ratio. Setup/projection/subdivision costs remain important. The existing
16-patch prepass adds cost and did not improve these fixtures. Do not extrapolate
that loss to a different architecture.

## Prove image identity before geometric shortcuts

Use the **complete actual painter sequence**, including merged/subdivided
terrain faces, models, mob parts, points and stable equal-key ties. Today the last
non-transparent write wins. Reverse that exact sequence and accept the first
non-transparent write: the same pixel must result, without an approximate
block-center sort. This preserves the current image, not proof that its existing
painter order is physically perfect. Do not draw all models in an independently
ordered pass that loses their relationship to terrain.

The coverage scaffold stores one bit per logical pixel: 2,000 B active at
160x100, 8,000 B at 320x200, with fixed maximum storage. It returns maximal
uncovered scanline runs. Mark only actual opaque/nonzero cutout writes;
transparent texel zero never writes color or coverage. Flat color zero remains
opaque: transparency comes from material semantics, not palette index alone.
Independent pixel-array tests cover overlaps, holes and exact sequence reversal.
Host correctness tests are not guest performance evidence.

The current assembly's transparent path skips texel zero; it does not perform
alpha blending. Exact cutout-aware coverage can therefore preserve these holes
without adding a new blended-transparency pass. This does not excuse changing
the order of models/cutouts, or treating palette zero as transparent for an
opaque primitive.

## Integration sequence and gates

1. Capture/replay actual projected primitives in a diagnostic build. Preserve
   global order and internal primitive/pixel order. Start with a simple reference
   sampler; compare every frame with the original, including cutouts, moving
   models and near clipping. Bound memory. On overflow, abandon the candidate
   and redraw the entire frame with the original, never drop commands.
2. Reverse groups and their internal commands exactly rather than storing full
   frames indefinitely. Reverse the static/entity merge's equal-key rules too.
   Model face/subdivision order must reverse inside each group. Bounded group
   capture can preserve ordering with less storage.
3. Emit uncovered spans using the **original full-row gradient, prestep, UV phase
   and integer rounding**. Calling the old trapezoid routine on shortened spans
   recalculates gradients and changes textures; it is not valid. Leave cutout
   holes uncovered. Existing reciprocal tables remain the arithmetic reference.
4. Measure opaque span rejection and full-viewport termination, then conservative
   tile summaries that reject primitives before raster work. Bounds rectangles
   are not filled coverage. Never skip the target outline/HUD on termination.
5. Move rejection earlier into traversal only with proven ordering/reference
   tests. Current voxel order schedules merged faces at slice/row boundaries;
   a naïve near-to-far center sort is not equivalent. Include models/openings.

Reset coverage every frame/viewport. Motion, mesh/light edits, presets and reload
must not reuse stale proofs. Preserve sky/clouds, outlines and the 40-row HUD.

## Measurement and adoption

Low is the primary performance gate. Neutral Low can be acceptable only with a
repeatable Medium/High benefit. Measure capture/coverage maintenance, setup,
samples/writes, draw/total frame time, tails and memory—not only rejected faces.
Test dense buildings, hidden geometry, doorways, forests/cutouts, caves, near
clips, model overlap and edits. Keep original-image checks and painter fallback.

Replay version 2 separately records the synchronous edit call and following
frame, permitting both their sum and render-only reporting. It still freezes
physics/AI and excludes fixture construction; it is not a comprehensive
simulation or first-edit-to-visible latency test.

## Audited implementation contract

Reverse the existing merge, not a new block-center distance sort:

| Existing painter | Exact front-to-back replay |
| --- | --- |
| Larger item keys first | Smaller keys first |
| Equal-key static items in reverse collection order | Collection order |
| Equal keys: entities before terrain (`>=`) | Terrain before entities (entity test `<`) |
| Reverse stable entity-part order | Ascending existing entity array |
| Subdivision/model faces in emitted order | Reverse their emitted primitives |

`f2b_capture.c` now provides caller-owned, bounded **per-item** command capture:
256 commands, each with up to 12 copied projected vertices plus the original
tile pointer, flags and flat color. This avoids a large full-frame vertex queue.
An overflow or unsupported primitive requires a complete original-painter frame
fallback; the module refuses to replay an overflowing prefix. Atlas data must
remain valid through replay. The capture module is now wired into diagnostic
builds only; it is not enabled by the normal playable builder.

The native test uses the actual pinned rasterizer and `trap.S` in 48 overlapping
scenes at both scales. Forward capture exactly matches the original. An
independent slow oracle draws each reversed primitive over backgrounds zero
and 255: equal results identify actual writes, including opaque zero, while
cutout holes remain unequal. First actual writes then reproduce the original
frame exactly, including guards/padding. This dual-background oracle is a test,
not the intended performance implementation.

In the actual trapezoid loop, query coverage after determining the original
clipped row bounds but **before** reciprocal/gradient setup. A fully covered row
can jump straight to edge/row advancement. For partially uncovered rows,
preserve the complete original gradient and left prestep, then advance the
existing fraction/byte-carry texture state across covered gaps. Bulk-mark only
opaque spans; cutouts mark individual nonzero samples. Query conservative
primitive bounds before edge setup when all enclosed pixels are covered.
Keep calls out of per-pixel hot loops, and measure capture/query overhead too.

The pre-subdivision audit also found a rounding hazard relevant to future
earlier rejection: weighted fixed-point plane tables can have interior minima
lower than both endpoints. The exact experimental parent gate confirms all
included grid lines only on a potential rejection; 120,000 real parent tests
prove descendants still fail the original gate. Its measured 0.6% walking-only
gain is not the architectural house improvement sought here and is not a
shipping default.

`f2b_texel.c` supplies the next arithmetic piece: advance the original texture
state over a covered gap without sampling it. It reproduces the existing
independent 8-bit integer address additions and 16-bit fraction carries, including
negative steps and no-wrap address-byte rollover. It does not recalculate a
gradient or clipping prestep; those remain the original complete-row values.
The caller still handles actual texel reads, transparent-zero decisions and
coverage updates. Do not insert a call for every pixel into the shipping loop.

Tests compare sparse sampling with the actual pinned `trap.S` for 20,480 rows:
all eight trap variants, every width 1–320, clipping, negative gradients, opaque
zero/cutout holes, wrapping and odd destination alignment. Every jump length
0–320 is independently compared with repeated real x86 ADD/ADC instructions.
Native and actual DJGPP i486 objects contain no calls, divides, FPU/SIMD or wide
math in this kernel. This proves phase correctness, not a speedup. Integration
must still skip fully covered rows before gradients and keep original-image
checks and full-frame fallback.

## Integrated diagnostic checkpoint

`--candidate front-to-back` selects explicit replayable DOS-only source overlays
in `tools/dos_front_to_back.py`; the 50-file Towns vendor pin is unchanged.
The original stable item/entity merge and every item's finalized primitive
sequence are reversed. Original collection, projection, adaptive subdivision,
LOD and clipping remain. Coverage begins after sky/clouds and ends before the
target outline, row doubling and HUD. The eight original trapezoid entry points
are link-wrapped; inactive calls use original assembly. Opaque uncovered runs
use `f2b_span.S`, retaining the original ADD/ADC pair loop and full-row UV setup.
Cutouts mark only nonzero writes. Conservative primitive rectangles query,
never fill, coverage. Full coverage ends only world traversal. Any item capture
failure redraws sky/clouds and the complete original painter frame.

Native tests pass 128 original-painter image scenes at both scales (including
non-word widths, non-power-two strides, guards, clipping, wrapping, flat/opaque
zero and cutout holes), 4,096 real-trap active/inactive cases, 360 actual opaque
assembly phase cases, invalid-target/coverage checks and nine DJGPP object/ABI
checks. The frontend tests exhaust stable key ties and recipe replay.

Three real 486DX/25 Low guest repeats additionally compared **every** frame's
world pixels against a fresh original-painter redraw: 228 frames per process,
684 total, zero mismatches and zero fallback frames. All last-frame hashes and
VGA-plane readbacks pass. Raw provenance/recipes are retained in
`tests/benchmarks/2026-10-10/f2b-low-every-frame-verify.json`.
`--verify-f2b` deliberately draws twice and labels its timings invalid;
`performance_compare.py` refuses those timings as performance evidence.

`--f2b-stress --verify-f2b` separately adds diagnostic-only mixed glass/leaves/
water/plants/torch/door terrain and overlapping rotated entity boxes near the
camera. It forces one genuine 256-command capture overflow to test whole-frame
fallback. These fixtures are not playable-world changes or comparable timings;
their guest result is still pending at this checkpoint. `--profile-draw` enables
existing 100 Hz IRQ EIP sampling during drawing only; its timings are also
rejected as performance evidence. The normal game remains byte-identical.

Performance/adoption is a separate gate. Compare input-isolated baseline and
candidate with the same hardware/settings, moving/down/house/edit means and
tails, then assess High only if Low is neutral or improves. No renderer default
or playable ISO has changed at this correctness checkpoint.
