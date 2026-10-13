# Actual reference-pixel import

The previous material passes were procedural interpretations. This revision
imports the actual texture pixels from the user's local Eaglercraft download.
79 of the game's 80 tiles use converted source pixels at full resolution;
the user-authored grass top remains original, per the earlier request.

`tools/import_eagler_textures.py` reads the extracted EPK resources offline.
`assets/textures/eagler_full.json` records the source path/hash, model UV crop,
conversion rule and resulting pixel hash for every tile. `eagler_full.bin`
holds the converted tiles; `eagler_full.inc` embeds them in the executable.
The DOS game needs neither the downloaded HTML nor PNG decoding at runtime.

## Small view corrections

| Material | Small-view art |
| --- | --- |
| Cobble | Exact pre-grain second-pass cobble, with clean mortar and quiet pieces. |
| Stone | Exact former full-resolution procedural stone. |
| Gravel | Exact former full-resolution procedural gravel. |
| Sand | Pale shade 15 with about 8% shade-14 flecks. |

These three restored tiles were compared byte for byte against the earlier
native preview outputs. Grass, leaves, wood and the other curated low-view
materials retain their existing patterns.

## Conversion adjustments

All imported pixels use the existing 256-color palette and unchanged shading
lookup. There is no added dithering. Gray stone stays in the gray ramp, dirt
in brown ramps, and vegetation/water receive their normal color tint. Dirt's
gray flecks become brown. Sand receives a 240/255 exposure adjustment so its
source detail does not collapse almost entirely into the brightest shade.
Opaque leaves retain opacity: reference alpha holes become dark green detail.

Animated water uses its first source frame. Bed and chest faces use their
actual model tiles/UV regions; the chest lid/base/latch are joined into our
single front tile. The bed icon uses source bed top, pillow and end pixels.
Pig snout pixels are placed on the head face because our pig has a flat face.
Mob body/face regions are cropped before resizing to 16x16; whole skin sheets
are never shrunk onto a face. Item silhouettes and glass use source alpha,
so their full-view masks can differ from the low-view masks. The existing
renderer refresh already updates the corresponding transparency flags.

This retains one 208,896-byte shaded atlas. Compiled source tiles add 20,224
bytes plus their 79 texture IDs; the executable grew from 516,755 to 536,760
bytes. Import/copy work runs only at initialization or style changes. Raster
loops, gameplay, terrain, save data and graphics settings are unchanged.

## Focused validation

`tests/test_block_textures.py` passes in about 3.3 seconds. It compares the
linked full-view tiles against all imported pixels, regenerates the converted
data from the local EPK when available, verifies the unchanged palette,
checks low-view alpha masks and repeats resolution switching with atlas reuse,
average-color refresh and distant-color cache invalidation. Every shaded atlas
texel is checked at every light level, and opaque mob crops have no holes.
The replayable source-overlay check and DOS release link also pass. No full
game suite, world generation, emulator gameplay smoke or 1992 test is repeated.

Review the two styles in `build/checks/texture-eagler-import/preview/resolution-styles.png`.
The reference comparison sheets and release are under the same check directory.

The shared ISO and separate biome preview are refreshed with this executable.
Both HDDs are preserved byte for byte, and the primary launcher passes
`-ValidateOnly`. [Style comparison](direct-import-styles.png) and
[publication evidence](direct-import-evidence.json).
