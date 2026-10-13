# Texture second pass

The requested revisions affect seven 16x16 tiles: grass top/side, dirt, water,
and the crafting table's top/side/front. Every other tile matches the first
pass byte-for-byte. Palette, transparency masks and atlas layout are retained.

- Dirt uses only the dirt ramp; the three grey mineral flecks are removed.
- Grass retains original DOScraft art. The initial broad shapes and a smaller
  lattice trial were rejected during review. The final tile uses fine random
  mottling, mostly single-pixel accents and incidental tiny clusters. The
  dominant shade is 12, light detail 13, and sparse dark detail 11. All detail
  repeats on the 16-pixel boundary; there is no coarse light-field motif.
- Water has one blue shade on 248 of its 256 pixels. Two broken ripple marks
  use the next adjacent shade. The old per-pixel noise and brighter bands are
  removed.
- The crafting table has a warm inset worktop, nine recessed cells, reinforced
  corners, framed timber sides and hanging tools. The supplied Eaglercraft top,
  side and front textures were inspected directly for composition. The game
  textures are generated original artwork in the existing palette.

Review [first/second pass](second-pass.png) and
[the final grass repeated 4x4](grass-second-pass.png). These show actual C
generator output with nearest-neighbor enlargement, not an AI mockup.

## Complete Eaglercraft comparison

`tools/compare_eagler_textures.py` maps all 80 game textures to available
reference assets from the supplied local EPK. Reference contact sheets and
the exact path mapping are in ignored output:

- `build/checks/texture-second-pass/reference-comparison/blocks.png`
- `build/checks/texture-second-pass/reference-comparison/mobs.png`
- `build/checks/texture-second-pass/reference-comparison/items.png`
- `build/checks/texture-second-pass/reference-comparison/mapping.json`

Tinted foliage/water references use our approximate fixed ramp colors so
material pattern is easier to compare. Animated references show the first
frame. Whole model sheets and face crops are labeled: their UV layouts and
model geometry differ from our 16x16 tiles. The bed item has a model-top
reference because the supplied build has no standalone bed item sprite.
Reference pixels are confined to the local review output.

The main opportunities identified for a later requested art pass are:

| Material | Observation |
| --- | --- |
| Furnace | Its mouth/grate is small and flat compared with the reference's deep opening and metal surround. Silhouette/readability matters more than adding noise. |
| Stone/cobble/gravel | Our blocks have broad, smooth patches. Finer restrained stone grain and smaller cobble pieces would read more naturally. Avoid large distinctive repeated islands. |
| Wood | Bark grain, end rings and plank joints are recognizable; the reference breaks up their very regular bands with smaller irregular features. |
| Leaves | The reference has stronger leaf shapes and transparent gaps. More shape detail is possible within our close shades; adding cutout holes would be a separate rendering decision. |
| Tools/ingots/food | The reference communicates thickness, bevels and distinct food silhouettes better. Our axes/shovels are very flat, and several foods share the same blob shape. |
| Mobs | Sheep/pig faces are readable; zombie/creeper detail and limb clothing could improve. A whole reference skin cannot simply be substituted for our repeated tiles or different model UVs. |

These comparisons informed the requested revisions; they are not approval to
redraw every other material. Grass remains our own design.

Native checks cover all shaded atlas texels, periodic grass detail, the bounded
shade range, sparse dark accents, brown-only dirt, mostly uniform water and
unchanged transparent texels. The full game build links with the existing
renderer flags; art still runs only during texture initialization.

The exact second-pass build passed June 1990 new/reload smoke and is published
to the shared ISO. The current player HDD is retained byte-for-byte. The biome
preview's ISO/boot media are refreshed to the same executable without replacing
its world disk. The primary launcher passes `-ValidateOnly`. See
[smoke and publication evidence](second-pass-evidence.json).
