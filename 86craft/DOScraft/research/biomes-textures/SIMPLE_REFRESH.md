# Simple texture refresh - 2026-10-14

Small-view art now follows the detailed compositions for door panels/windows,
all crafting-table faces, the red poppy, every bed surface and the furnace cap.
Stone brick uses exactly two flat gray colors and staggered mortar. Natural
textures, ores, leaves, TNT, chest art and full-resolution block art are retained.
The door keeps its light brown frame, dark recessed panels and gray hinges.
Bed covers are flat red with a broad seam; the pillow retains its white shape.
The furnace cap is smoothed offline to retain broad source stone facets.

All twelve existing tools/weapons use the supplied Eaglercraft silhouettes in
both styles. The full version retains reference detail. Its handle colors now
quantize to the brown ramps, avoiding olive handles. The simple version uses
two wood shades and two material shades, preserving exactly the full alpha mask.
Full-resolution material mapping changes only the twelve tool tiles.

`tools/simplify_textures.py` creates 25 selected simple tiles and provenance in
`assets/textures/eagler_simple.*`. Generation/import are offline. Pixel copies
run during existing startup/style rebuilds; rendering, palette, atlas dimensions,
allocation, saves and gameplay are unchanged. The simple constant table contains
6,400 bytes of pixels; no additional shaded atlas is allocated.

Validation follows AGENTS.md's texture-only policy: texture previews and the
focused real native atlas/style-switch tests. Both tests in
`tests/test_block_textures.py` pass, including renderer averages/transparency,
leaf opacity, atlas pointer/allocation reuse and distant-cache invalidation.
No new/reload or world-generation emulator suite was run for these art changes.

The release uses the preceding held-item production flags. All platform objects
and every imported object except `obj/textures.o` are byte-identical to that
exact smoke-tested baseline. Texture-only publication records this evidence
rather than claiming a new exact-EXE emulator smoke. Media validation passes,
the shared ISO is updated, and the player HDD is copied byte-for-byte.
Publication hashes and baseline smoke provenance are in `SIMPLE_REFRESH.json`.

Review sheets are generated in `build/checks/simple-texture-refresh/`:
`resolution-styles.png`, `updated-blocks.png` and `tool-styles.png`.
