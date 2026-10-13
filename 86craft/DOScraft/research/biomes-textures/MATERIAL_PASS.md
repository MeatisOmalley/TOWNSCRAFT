# Reference-informed material pass

This pass follows the user's detailed Eaglercraft comparison. The accepted
fine-random grass top and revised crafting table are retained. The 14 changed
tiles are grass side/dirt, cobble, leaves, sand, water, four ores, furnace front,
wool, stone brick and sheep wool. Shared dirt and wool generators account for
the grass-side soil and sheep-coat changes.

Review [previous/revised materials](materials.png). Updated direct reference
sheets for all 80 textures are in
`build/checks/texture-material-pass/reference-comparison/`.

| Material | Revision |
| --- | --- |
| Dirt | Three visible brown shades, finer granular detail and no grey pixels. |
| Cobble | Fine light/dark stone grain inside the irregular mortar-separated pieces. |
| Leaves | Curved leaf outlines and short veins over a darker green background. Gaps remain opaque texture detail. |
| Sand | Brighter sandy base with fine adjacent-shade grain, instead of large pale patches. |
| Water | Mostly continuous middle blue with gentle darker/lighter fields and sparse fine variation. Three close shades replace the almost solid previous tile. |
| Ores | Eight irregular deposits distributed across the entire stone matrix, with faceted highlights and shadows. Every ore reaches all four texture quadrants. |
| Diamond | Brighter cyan, pale blue and white highlights replace the flat teal deposits. |
| Gold | Brighter golden yellow, pale warm highlights and brown/gold shadows. |
| Furnace | A larger dark fire opening, bright red embers and yellow flame tongues. The side/top textures retain their prior art. |
| Wool | The brightest available white dominates, with restrained soft fibre detail. Sheep coats inherit it. |
| Stone brick | Softer mortar/face contrast and restrained stone grain. |

This uses the existing fixed palette: brightest white is RGB 235/235/235 and
brightest red is 210/40/40 before VGA quantization. Diamond/gold highlights
combine existing suitable ramps. No global palette change is required, so
the texture revision does not recolor unrelated UI or materials. The supplied
reference colors are inspiration, not a claim of exact RGB matching.

All artwork remains generated original 16x16 art. Tile counts, transparent
texels, atlas dimensions, lighting lookup rules and raster loops are retained.
Native checks compare every shaded atlas texel and transparency mask, check
the three dirt tones, water variation/dominant base, white wool, bright furnace
reds, bounded stone-brick contrast and ore coverage across all quadrants.
The exact full-game build is tested with one June 1990 new/reload smoke;
the biome seed is already baked and does not need regenerating for texture art.

The exact material build passed June 1990 new/reload smoke and is published
to the shared ISO. The biome preview uses the same refreshed game media. Both
world disks are retained byte-for-byte, and the primary launcher passes
`-ValidateOnly`. [Native/smoke/publication evidence](material-pass-evidence.json).
