# Biome world and restrained block textures

The first biome pass adds plains, forests, deserts and rocky highlands to the
offline 1024-world baker. Climate chooses surface materials and vegetation;
continentalness, erosion and ridges shape land independently. Smooth domain
warping breaks up straight noise-grid contours. Low winding valleys create
river-like channels and lakes. These are not simulated drainage networks.

The game loads ordinary prebaked block records, baked light and the existing
distant surface map. No biome lookup table or new terrain noise runs in DOS.
World height remains 48 blocks. Caves and ores use the existing baker, and the
four regions use existing blocks. Tree placement peaks at 32/1000 candidates,
below the legacy forest peak of 45/1000. Geometry density can still affect FPS.

## Supplied Eaglercraft reference

Inspected the local `eaglercraft-26.2-zeus-skins.html` statically. Its SHA-256 is
`8c367ef75db61f069cfb492d8b15f79d5ab64ecf4b6f4f6736af6018f19d3c09`.
The page embeds Brotli-compressed WebAssembly and an EPK resources package.
The EPK's own header identifies an export on 2026-10-09. Its actual worldgen
resources include:

- `data/minecraft/worldgen/noise_settings/overworld.json`: separate continents,
  erosion, ridges, temperature, vegetation, depth and final-density inputs.
- `density_function/overworld/continents.json`, `erosion.json` and `ridges.json`:
  shifted noise in X/Z, with a two-dimensional cache.
- `offset.json` and `factor.json`: spline-driven combinations of terrain fields.
- `depth.json`: a vertical gradient combined with terrain offset.

Those resources describe a 384-block vertical span starting at -64. A direct
copy does not suit our 48-block world or current heightfield-plus-worm-cave
baker. This pass is an original smaller implementation of the field separation
and coordinate-warping ideas; it does not claim Minecraft generation parity.
The extracted reference resources and images remain in ignored local study
output. The game uses original generated artwork.

## Texture pass

The later [second pass](research/biomes-textures/SECOND_PASS.md) supersedes the
grass, dirt, water and crafting-table details below. Grass now uses fine random
detail rather than broad patches, water is calmer, and the workbench follows
the reference composition more closely. Other first-pass art is retained.

The subsequent [material pass](research/biomes-textures/MATERIAL_PASS.md)
revises dirt, cobble, leaves, sand, water, ores, furnace fire, wool and stone
brick from the user's direct reference feedback. It retains the fine grass
top and second-pass crafting table.

Updated the common terrain/building textures and wood-based surfaces while
keeping the 16x16 format. Grass and leaves now use two adjacent shade levels,
rather than widely separated green values. Grass is 12/13; leaves are 11/12
instead of the legacy 8/11/13. Small grouped motifs provide texture without
bright isolated flecks. Dirt and stone have subdued mineral patches, sand has
quiet grain, cobble has irregular mortar-separated stones, and wood has bark
grain, end rings and staggered plank joints. Ores and wooden furniture inherit
the revised stone/wood bases. Model and item artwork is not a complete redraw.

The palette and lighting ramps are retained. The generated shaded atlas has
the same tile count, dimensions and allocation. Native checks compare every
atlas texel at every light level and verify unchanged transparency masks
against the original generator. The linked executable grew by 1,062 bytes
(512,189 to 513,251); this art runs at startup, not in the raster hot loop.

Review [before/after block art](research/biomes-textures/textures.png),
[tiled grass](research/biomes-textures/grass.png), and
[sampled geography](research/biomes-textures/geography.png). Geography previews
show ground heights and climate regions; they do not show canopies or caves.

## Baked candidate and testing

Seed 4242 with `--style biomes` has these land cells above the beach level:

| Region | Land cells |
| --- | ---: |
| Plains | 91,850 |
| Forest | 75,981 |
| Desert | 35,181 |
| Highlands | 85,915 |

The bake has 262 cave entrances, ground heights 4-38 and grassy spawn
`524, 537`. The biome definition is implicit in the baked blocks; the seed
identity/checksums distinguish this geography from legacy seed 4242. Do not
migrate old edits onto it simply because the numeric seed matches.

`tools/bake_large_world.py --style legacy` retains the original generation
recipe and remains the CLI default for reproducibility. Use `--style biomes`
for this new world, then the existing lighting and distant-map append tools.
The bake's report option records region coverage, spawn and cave entrances.

The exact new texture executable passes June 1990 new-game and fresh-process
reload smoke on the biome world: streaming, V5 persistence, torch lighting,
graphics presets, distant terrain, inventory, menus and VGA readback. Four
biome tests, two texture tests and four distant-world/render tests pass.
No 1992 VM is involved. This is not a full gameplay FPS or exhaustive visual
acceptance result.

## Textures selected by resolution

Small views (160x100 and Very Low) use a curated simpler set. Full-resolution
views use actual palette-converted pixels imported from the supplied Eaglercraft
bundle, with the user's custom grass top retained. Actual resolution selects the art,
so Custom settings work too; there is no additional settings control.

Small-view cobble is the accepted version before fine stone grain was added.
Stone and gravel are the former full-resolution procedural versions. Sand has
a pale base and sparse adjacent-shade specks. Smooth water, ores, crafting
table, furnace fire, white wool and softer brickwork retain their procedural
updates. Dirt retains three adjacent browns with sparser detail; foliage and
wood stay quiet.

Only one shaded atlas stays resident. Resolution changes rebuild
it in place and refresh renderer averages and distant-color caches. The
panorama's terrain samples stay cached. Raster loops and the palette remain
unchanged; there is a single no-op style check at frame entry for direct scale
changes by diagnostic code. The imported tiles add about 20 KB of compiled
read-only data, rather than a second shaded atlas. Conversion happens offline.

For texture-only changes, run `tests/test_block_textures.py` and review the
preview from `tools/preview_block_textures.py`. This native check switches both
ways repeatedly, checks every shaded atlas texel, verifies the unchanged palette
and small-view transparency, confirms all imported tiles match their baked
pixel data, verifies renderer transparency flags and average colors and
distant-cache invalidation, and checks that atlas allocation occurs only once.
Do not repeat world-generation/new-load gameplay smoke for a few pixel edits.
The style-switch build is validated by these focused checks and the DOS link;
it has not rerun the emulator gameplay suite.

See [direct import details](research/biomes-textures/DIRECT_IMPORT.md), including
source mappings and the small material adjustments needed for our palette.

The separate playable biome VM is recorded in
`build/checks/biome-texture-study/player-vm.txt`; use
`build/checks/biome-texture-study/Play-Biome-Preview.cmd` to open it. It has its
own prebaked HDD and save. Enable distant terrain in Graphics to inspect the
new horizon. New geography needs a new world; the current player's terrain
and edits are preserved during the shared ISO texture update.

The texture update is published to `ISO/DOSCRAFT.ISO`. The current player HDD
was copied byte-for-byte, and the biome preview contains an identical playable
ISO with a separate world disk. Both the biome seed and existing seed passed
new/reload smoke using the exact executable; the current launcher passes
`-ValidateOnly`. Twenty focused tests pass in total, including the retained
continuous-walk benchmark tests. See [recorded evidence](research/biomes-textures/evidence.json).
