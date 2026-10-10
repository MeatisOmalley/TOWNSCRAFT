# Exact front-to-back experiment

This is a development plan and correctness scaffold, not a shipping optimization
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
