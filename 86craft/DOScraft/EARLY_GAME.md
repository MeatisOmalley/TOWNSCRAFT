# Primitive early game: sticks, stone, clay, copper, bronze

Design, 2026-10-15. It replaces the Minecraft ladder (wood, stone, iron) with
a historical one and keeps the existing iron and diamond tiers after it.
Saves are not preserved: every change here may rebuild the world. Numbers are
starting values to tune.

## Implementation status (2026-10-15)

Built on branch `claude/next-feature-planning-cc05c4`, one commit per feature.
Game logic is the `tools/dos_early_game.py` overlay (fields and hook calls in the
pinned sources) and `src/platform/dos/early_game.c` (the systems themselves).

- Tool durability, wear bars, wear kept through moves; large-world save
  version 7 with an extension section (wear, hunger, fires, sheared sheep).
- Terracotta palette ramp and 38 reviewed tiles (`tools/early_game_textures.py`).
- New blocks, items and tiers; bare-hand drops (logs need an axe, cobble is
  hand-collectable, gravel gives flint and tin, clay four lumps).
- One recipe list (`RECIPES`) for the recipe table and named grid patterns;
  stone anvil, campfire with its own fuel, clay furnace, bronze, bellows and
  bloomery (iron smelts only there).
- Spear jab and charge, hunger, double-tap W sprint, sheep plucking, water
  filling.
- World: `tools/bake_large_world.py` overlays the materials after all other
  generation (`--no-early` reproduces the old world byte for byte), and
  `tools/check_world_contract.py` checks the result. Seed 4242 bakes in about a
  minute: base, then `bake_large_light.py`, then `bake_far_field.py
  --crown-leaves`. Clay and copper show as dirt and stone from a distance
  (`FAR_REMAP`, `dos_far_id`).

Not yet: bench upgrade tiers (the anvil and crafting table are separate),
binding, farming, rain putting out fires. Wall torches, doors and the new
blocks have not been tried by hand in the emulator.

## Philosophy

1. **Tools are earned, not conjured.** Every tool is something a person could
   make from what they already hold. No wooden pickaxe, no wooden sword.
2. **Every block has a job.** Gravel gives flint and tin. Cobble is loose
   stone. Leaves give sticks and apples. Clay becomes bricks.
3. **Geography drives progress.** Materials sit where they would in the real
   world. Rivers are the cradle (clay, gravel, flint, tin, river cobble).
   Highlands hold copper. Forests hold wood.
4. **Hands can break anything.** The tool decides only how fast a block breaks
   and whether it drops anything.
5. **Everything wears out.** Every tool has durability. Better metal means a
   longer life, not just faster mining.
6. **The base records progress.** The bench upgrades in place, with the fire and
   furnace beside it.

## Bare-hand drops

| Block | By hand | Notes |
| --- | --- | --- |
| Leaves | Already drops an apple (1 in 12) or else a stick (1 in 4), about 23% sticks overall (`game.c:248`) | Breaks almost instantly. The stick supply. The current rate stays. |
| Log | Breaks slowly, drops nothing | Needs a stone axe. |
| Gravel | Flint 10%, otherwise gravel. Tin-bearing gravel adds a rare tin roll (start at 1%). | Hardness rises from 12 to about 40 (about 2 s by hand). Flint and tin consume the block. |
| Cobble (natural) | Cobble | No longer needs a pick. This is the loose stone. |
| Stone | Breaks, drops nothing | A stone pick makes it drop cobble. |
| Clay | 4 clay lumps | About 2 s by hand, fast with a shovel. |
| Dirt, sand | As now | |
| Copper ore | Breaks, drops nothing | Needs a stone pick. Texture copied from Eaglercraft's copper ore. |

## Recipes in order

**Hand grid (2x2)**
- Fire: 2 sticks side by side. Lights when placed.
- Stone anvil: 4 cobble in a square. The first bench, with a 3x3 grid.
- Flint spear: stick spear + flint (an upgrade that consumes the stick spear).
- After the axe: 1 log to 4 planks, 2 planks to 4 sticks.

**Stone anvil (3x3)**
- Stick spear: 3 sticks diagonally. The first weapon (a sharpened,
  fire-hardened stick).
- Stone axe: 3 cobble + 2 sticks. Logs now drop wood.
- Stone pick: 3 cobble + 2 sticks. Stone drops cobble; copper ore can be mined.
- Wooden shovel: 1 plank + 2 sticks (needs the axe first, for planks).
- Clay furnace: 8 clay bricks in a ring.

**Fire (a station with a fuel slot, like the furnace)**
- Fuel: sticks (about 1.5 min each), planks, logs (about 6 min), charcoal
  (about 12 min). Burn time caps at about one in-game day.
- Raw meat to cooked meat.
- Clay lump to clay brick.
- Log to charcoal.
- Gives light at night.

**Clay furnace**
- Copper ore to copper ingot, with charcoal.
- 10 copper + 1 tin to 11 bronze ingots.
- Cannot smelt iron (a clay furnace reaches about 1100 C; iron needs 1538 C).

## The first day

Strip leaves for sticks and apples and light a fire. Collect 4 cobble, build
the stone anvil and make a stick spear. Dig gravel for flint, upgrade the
spear to flint, hunt and cook. Make the stone axe, then planks, then the
wooden shovel. Dig clay, fire bricks and build the clay furnace.

## Bench tiers

One station that upgrades in place by using materials on it. Each recipe
carries the minimum tier it needs, replacing today's yes/no
`recipe_available(r, station)` check.

| Tier | Name | Upgrade with | Makes |
| --- | --- | --- | --- |
| 0 | Stone anvil | (4 cobble) | Spears, stone axe and pick, wooden shovel, clay furnace |
| 1 | Anvil with a plank work top | 4 planks | Doors, chests, beds |
| 2 | Mason's Bench | Clay bricks | Cut stone, copper tools |
| 3 | Metal anvil | Bronze ingots | Bronze and iron tools |

The stone anvil replaces the earlier Stick Frame idea. Early toolmakers worked
on an anvil stone: a large flat rock you rest a flint nodule on and strike with
a hammerstone. Four cobbles in a square fill the 2x2 hand grid, cannot clash
with the fire's two sticks, and the first trip becomes "collect stones, dig
flint". It is naturally a cube: a stone block with a worn, pitted top.

Open: carpentry (the plank tier) is not anvil work. Either keep one upgrade
line as in the table, or split it into a stone-then-metal anvil for tools and
a separate plank Workbench for doors, chests and beds. Splitting costs one more
block ID, which stops mattering once the ID space is widened (below), so it can
be decided on design alone.

## Tools

| Tier | Pick | Axe | Shovel | Weapon |
| --- | --- | --- | --- | --- |
| 0 Hand | - | - | - | Fist |
| 1 Stone/wood | Stone pick | Stone axe | Wooden shovel | Stick spear, flint spear |
| 2 Copper | Yes | Yes | Yes | Copper spear |
| 3 Bronze | Yes | Yes | Yes | Bronze spear, first sword |
| 4 Iron | Yes | Yes (new) | Yes (new) | Iron spear, sword |
| 5 Diamond | Yes | Yes (new) | Yes (new) | Sword |

No knife, no stone shovel, no binding. The sword arrives with bronze.

| Tier | Speed multiplier | Durability |
| --- | --- | --- |
| Hand | 1 | - |
| Stone / wood | 2 | 130 (wooden shovel 60) |
| Copper | 4 | 190 |
| Bronze | 5 | 450 |
| Iron | 6 | 600 |
| Diamond | 9 | 1500 |

A tool loses 1 durability per block mined or hit landed, and 2 when used for
the wrong job.

## Spear

Matches the Minecraft 26.x spear that Eaglercraft 26.2 uses.

- **Jab** (attack key): reach 2 to 4.5 blocks, and it cannot hit anything closer
  than 2 blocks. No critical hits; cannot break blocks.
- **Charge** (hold use): after an activation delay, damage is a tier multiplier
  times closing speed. It only hits above 4.6 blocks/s closing speed and knocks
  back above 5.1. It can hit several targets and fades through three stages.

| Spear | Jab damage | Jab cooldown | Charge multiplier | Charge delay | Durability |
| --- | --- | --- | --- | --- | --- |
| Stick | 1 | 0.65 s | 0.7 | 0.75 s | 59 |
| Flint | 2 | 0.75 s | 0.82 | 0.7 s | 131 |
| Copper | 2 | 0.85 s | 0.82 | 0.65 s | 190 |
| Bronze | 3 | 0.9 s | 0.9 | 0.6 s | 450 |
| Iron | 3 | 0.95 s | 0.95 | 0.6 s | 600 |

(Stick, flint, copper and iron follow Minecraft's wooden, stone, copper and iron
spears. Bronze is interpolated; durability uses this game's tool table.)

Engine check: walking is about 4.2 blocks/s, sprint about 5.5, zombies about 1.9.
Walking into a zombie closes at about 6.1, so the charge hits. A panicking sheep
flees at about 2.3, so sprinting after it closes at only about 3.1: the charge
misses fleeing animals and you jab them, as in Minecraft. Today's
`do_attack` has one reach for everything, so the 2-block minimum is new.

## Copper, tin and bronze

- **Copper is cheap.** Common, shallow highland ore; one ore gives one ingot.
  Spending it on tools costs nothing, so hoarding makes no sense.
- **Tin is the constraint.** 10 copper + 1 tin gives 11 bronze, which is about a
  full bronze kit, so scarcity comes from how long it takes to find the first
  tin, not from needing a lot of it.
- **Only some rivers carry tin.** The baker sets a metadata bit on gravel in tin
  country. It costs no block ID, looks the same, and placed gravel never has the
  bit, so mining and replacing gravel cannot farm tin.
- **Copper tools speed the tin hunt.** A copper shovel digs gravel faster (more
  tin rolls per hour); a copper pick mines the copper bronze needs faster.
- **Target:** about 1-2 in-game days to the first tin. A tin river then gives a
  steady supply. Bare-hand tin farming should feel slow enough that a shovel
  comes first.
- **Iron:** early bloomery iron was not harder than bronze; it won because iron
  ore is everywhere. Keep the bronze-to-iron jump small and iron ore common.
- **Later, optional:** worn metal tools melt back into an ingot.

## Hunger

- A 20-point bar next to the hearts. It drains with time and effort
  (sprinting, jumping, mining, attacking). Tune so a full bar lasts about one
  20-minute day of normal activity.
- Health regenerates only when hunger is near full, and regen costs hunger.
  Below 6 hunger you cannot sprint. At 0 you take starvation damage down to
  1 heart.
- Food fills hunger instead of healing directly. Existing values already favor
  cooking: pork 3 raw / 8 cooked, mutton 2 / 6, chicken 2 / 6.
- No hidden saturation stat in v1. No raw-meat sickness unless cooking still
  feels optional.

## Sprint

Sprint already exists: Ctrl while moving forward gives x1.3 speed
(`game.c:1698`, `player.c:106`). Add double-tap W: a second press within about
7 ticks starts it; it continues while W is held and stops on release, a wall,
water or low hunger. The AT keyboard repeats key-down codes while held, so
detection must use the not-pressed-to-pressed edge in `g_keyDown`.

## Water filling

Water does not flow or spread. When the player breaks a block, the emptied cell
fills with water if a side or top neighbor is water. Digging a trench from a
lake therefore fills cell by cell as you dig. When a cell fills, any air
directly below it also fills, down to the first solid block (bounded, about 8
cells), so water does not float over a hole. Nothing else moves water; placing a
block in water replaces it, as now. Each fill is an ordinary edit, so it costs
the same lighting and mesh work as breaking a block and is saved in the edit
log.

## Sheep

Use on a sheep with an empty hand to pluck 1 wool. The sheep shows as sheared
and regrows after about a day. Killing gives mutton plus 1 wool. This matches
early sheep, whose fleece was plucked by hand long before shears. A bed (3 wool)
is a reachable early goal.

## Where materials generate

Clay settles out of slow or still water and builds up as a subsoil layer in
warm, wet ground. It is rare on steep rocky ground.

| Material | Placement |
| --- | --- |
| Clay | Lake and river beds (1-3 layers), banks within about 3 blocks of water (under the dirt), forest subsoil lenses (3-6 wide, 3-4 deep), rare desert dry-lakebed patches. None in highlands. |
| Gravel | Common in riverbeds, lake beds and highland scree. Tin bit only in tin country rivers. |
| Cobble | Talus at the foot of highland cliffs, riverbank and lakeshore cobble beds, cave mouths, a few 1-3 block boulders in plains and forest. |
| Copper ore | Common and shallow in highlands and hills. |

### Gravel today

In the current 1024-world bake gravel is close to absent:
- `terrain_biomes.surface()` returns sand for any column at or below sea
  level + 1, which overrides the underwater gravel rule in
  `bake_large_world.py` whenever biomes are on.
- Gravel appears only as a highland surface block at height 26 or more, where
  slope is 3 or more or detail noise is above 150, and only when detail noise
  is above 210. Otherwise that cell is stone.
- Nothing places gravel underground or in riverbeds, and nothing places cobble.

### World-generation contract

Gameplay depends on the baked world meeting these rules. Each one becomes a
check in a script (for example `tools/check_world_contract.py`) that scans a
baked image and fails the bake, not only a line in this file.

- Clay exists in lake and river beds, on banks, and in forest subsoil lenses.
- Gravel is plentiful in riverbeds and lake beds, with tin country rivers.
- Copper ore is common in highlands within reach of the surface.
- Cobble exists near cliffs, cave mouths and shorelines.
- The spawn point is within a set distance of water, trees, gravel, cobble
  and clay.

## Textures

Reviewed as mockups on 2026-10-15. Nothing is imported yet.

- **Palette:** all 256 VGA colours are used, as 16 ramps of 16 shades, and
  lighting darkens a texel within its own ramp (`g_shadeLUT`,
  `textures.c:835`). A new colour needs a whole ramp. Plan: replace `R_OLIVE`
  (95,155,85) with a terracotta ramp (205,112,72). It gives red clay, red
  bricks and orange copper. Re-import the Eaglercraft tiles afterwards and
  recheck anything that used olive (zombie skin, rotten flesh).
- **Clay block:** red, from Eaglercraft `clay.png` recoloured on the
  terracotta ramp, using the "less noise" gradient: (112,52,36) at the
  darkest, (140,66,46) at the middle, (158,80,56) at 85%, (172,94,66) at the
  brightest.
- **Clay lump:** Eaglercraft `clay_ball.png` on the same ramp, keeping the
  higher-contrast gradient so its dark outline still reads in the hotbar:
  (80,34,24), (138,64,44), (170,88,62), (188,108,80).
- **Copper:** ore, ingot and tools straight from Eaglercraft once the
  terracotta ramp exists. Bronze and tin are recoloured from copper and raw
  copper. The flint spear is the stone spear with a flint-grey head.
- **Clay furnace:** terracotta, matching the clay and the red bricks it is
  made from (iron-rich clay fires redder). Sharp block edges; an arched
  firing hole with flames and logs on the front; smoothing marks and a
  drying crack on the sides; a round opening with embers on top. Give the
  firing hole a heavier soot ring so it reads against the darker body.
- **Firing shows on the brick:** the clay lump stays dull red-brown and the
  fired brick is brighter orange-red.
- **Stone anvil:** a knapping station rather than a plain stone block. The
  top has a worn, smooth working face with peck marks, a dark flint nodule
  with chalky cortex, a pale hammerstone in the top-right corner and
  scattered flakes. The sides are darker toward the edges so it reads as a
  rounded boulder, with flake scars along the rim and earth at the base.
  Chosen over a later pass that shaded the hammerstone and lightened the
  flint.

## Engineering constraints

Found by reading the source on 2026-10-15.

1. **Block IDs: 6 bits (built 2026-10-15, overlay `tools/dos_block_ids.py`).**
   A world cell is one byte; it is now a 6-bit block ID and 2 bits of
   metadata (`BLOCK_IDS` = 64), up from 5 and 3. Every existing block keeps its
   ID, and baked terrain is byte-identical: it never sets bit 5, and the baked
   leaf rim flag (bit 6) stays a metadata bit. Torches and doors needed the
   third bit, so wall torches (30) and open lower and upper doors (31, 32) are
   their own blocks; `DOOR_META` rebuilds the old door state. Items start at
   64. Block tables are sized to all 64 IDs, because mesh lookup tables index
   every byte (they already read past the 30-entry table before). The platform
   headers get the same overlay (`build_adapters.stage`), or platform code
   would compile against the old layout. The far-terrain map keeps its own
   5-bit material byte (`FAR_ID`, `far_field.h`); blocks with IDs of 32 or more
   are skipped as distant surfaces until it gets a remap. Saves written before
   this change store old item IDs, so the large-world save is now version 6
   and the game neither offers nor loads version 5 (media tools still read
   both layouts). Tested by `tests/test_block_ids.py` and the large-world
   guest smoke; published 2026-10-15 to the 1990 player (1992 refreshes on
   its next launch) and the 386 player. Wall torches and doors have not been
   tried in the guest.
2. **No durability.** A `Slot` is `{u8 item, count}` (`inventory.h:8`). Add a
   wear field to inventory slots, chest slots, the craft grid and the save
   codec.
3. **Station state.** Furnace fuel is one global counter (`g_furnaceFuel`).
   Fires and furnaces need fuel per block; benches need a tier. Today's block
   definitions cannot show a different texture per metadata value, so bench
   tiers need either their own block IDs or per-metadata textures.
4. **Recipes.** `g_recipes` (`inventory.c`) and the shape patterns in
   `src/platform/dos/craft_grid.c` change together. The grid is 2x2 by hand and
   3x3 at a bench; patterns can shift and mirror; each filled cell consumes one
   item.
5. **Baker.** New placement in `tools/bake_large_world.py` and
   `tools/terrain_biomes.py`, plus the contract checker.
6. **HUD.** The hunger bar needs space in the 40-row HUD and must work with the
   386's unchanged-row skipping.
7. **Combat.** Spear reach band, jab cooldowns and the charge attack are new in
   `do_attack` and the input code.
8. **Mobs.** The sheared sheep needs a saved flag and a model or texture swap.
   Check whether the mob record has a spare bit.

## Shipping slices

1. Block ID space and durability (no visible change).
2. Stage 0-1: sticks from leaves, fire, stone anvil, stick and flint spears,
   stone axe and pick, cobble and gravel placement, hunger and sprint.
3. Clay, bricks and the clay furnace.
4. Copper, tin and bronze.

## Open

- Fire burn times.
- Tin drop rate and how many tin country rivers a world has.
- Bronze spear and sword numbers.
- How far the spawn point may be from each starting material.
