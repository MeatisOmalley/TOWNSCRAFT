# Chickens

Chickens join pigs and sheep in the seeded passive population. Newly inspected
grass regions select among the three animals; existing visited regions and
saved animals are preserved. In an existing save, explore new grassy areas to
find the new population. No terrain rebake or new world is required.

Chickens have four health points, a smaller collision body, passive wandering,
panic when hit, obstacle hopping and slow descent. A seven-box model supplies
the white body/head, yellow bill and feet, red facial wattle, stepping legs and
an airborne wing pose. Small view uses quiet procedural art; full resolution
uses cropped/tinted pixels from the supplied chicken skin and food/item assets.

Player kills give one raw chicken and zero to two feathers. Raw chicken restores
two health points; cooked chicken restores six. The existing furnace cooks raw
chicken with normal fuel costs. Feathers are a collectible drop. Egg production,
breeding and new crafting recipes beyond cooking are not included.

The existing passive/hostile caps and 24 simulation slots remain. At the largest
population, 14 seven-box chickens, eight six-box hostiles and two TNT boxes use
148 of the existing 160 render boxes. Three new shaded textures add 12,288
atlas bytes; the atlas still allocates only once. New item IDs 59-61 and mob ID
6 are appended. Existing IDs, mob record layouts and the V5 save format remain;
older saves load without migration. Chickens despawn beyond 32 blocks like the
other mobs, and no distant animals accumulate in memory.

`tests/test_chickens.py` exercises actual staged mob code with the real physics,
inventory, item definitions, math and save codec: spawning across seeds, movement,
collision/hit selection, passive behavior, slow falling, all model parts, combat,
drops, cooking/fuel, valid and invalid save records, existing ID stability,
population/render budgets and despawning. The separate texture test verifies
both styles, all shaded texels and in-place atlas rebuilds. Guest acceptance uses
one June 1990 new/reload smoke run with the existing prebaked world and internal
AT input. It checks a saved chicken and food/feather inventory survive loading.

All four native checks and both guest smoke phases pass on the exact release
executable. The shared ISO and biome preview are published with this build;
their HDDs remain byte-identical, and the primary launcher validates. The
diagnostic VM is closed. [Recorded evidence](research/chickens-evidence.json).
