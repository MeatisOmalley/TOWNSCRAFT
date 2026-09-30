# TOWNSCRAFT

A Minecraft-style block building game for the FM TOWNS, written to run on
the Tsugaru emulator in this repository (and, in principle, on real
hardware).  It boots directly from a CD image, with no TOWNS OS needed.

It is far from Minecraft's feature set, but everything listed under
Features works in the emulator at interactive frame rates.

## Running

```
Tsugaru_CUI <ROM directory> -CD TOWNSCRAFT.ISO -BOOTKEY CD -DIFFMOUSE
```

`-DIFFMOUSE` passes relative mouse motion to the emulated mouse (the game
reads the mouse directly, without the TOWNS OS mouse driver that Tsugaru's
default mouse mode relies on).

`TOWNSCRAFT.ISO` is prebuilt in this directory.  Machine settings:

| Setting | Minimum (base spec) | Recommended |
|---|---|---|
| Machine | `-TOWNSTYPE MODEL2` (386DX) | Tsugaru default or `-TOWNSTYPE MX` |
| CPU clock | `-FREQ 16` | default (faster) |
| RAM | `-MEMSIZE 2` | 4 MB or more |
| FPU | not needed (the game is integer only) | |

The game sizes the world to the available RAM (96x96 blocks with 2 MB,
160x160 with 4 MB, 208x208 with 6 MB, 256x256 with 8 MB or more; 48 blocks
high) and picks a render resolution and view
distance from a quick CPU speed check at startup.  Both can be changed in
game (PF2, PF3).

Measured in Tsugaru with the automatically chosen settings (160x100
rendering of a 320x200 view above the HUD strip), standing at the spawn
point of a fixed test world and looking in 8 directions (see Testing):

| Machine | View distance | Frame rate (average, range) |
|---|---|---|
| Model 2, 386DX 16 MHz, 2 MB | 10 | 10.7 fps, 9-14 fps |
| Tsugaru default profile, 4 MB | 16 | 18.5 fps, 15-25 fps |

## Controls

| Key | Action |
|---|---|
| W A S D | Move |
| Arrow keys (or numeric keypad 8/4/6/2), mouse | Look |
| SPACE | Jump / swim up |
| CTRL | Sprint |
| J (hold), left mouse button | Break block / attack |
| K, right mouse button | Use (door, bed, crafting table, furnace, TNT), place block, eat |
| 1-9, `,` `.` | Select hotbar slot |
| E | Inventory (SPACE picks up / places a stack, Q discards) |
| C | Crafting by hand |
| ESC / PF1 | Help |
| PF2 | Toggle 320x240 / 160x120 rendering |
| PF3 | Cycle view distance |
| PF4 | Debug overlay (frame rate, position, time) |

A game pad on port A also works: pad to move and turn, A to break/attack,
B to use/place, RUN to jump, SELECT to change the hotbar slot.  The mouse
goes on port B.

## Features

- Island worlds: grass, dirt, sand, gravel, stone with coal and iron ore,
  bedrock, water, trees, flowers and tall grass.
- Breaking with tool tiers (wood, stone, iron pickaxes, axes, shovels) and
  placing blocks.
- Crafting by hand, at a crafting table and at a furnace (smelting uses
  coal, logs, planks or sticks as fuel).  Recipes include planks, sticks,
  crafting table, torches, furnace, tools, swords, doors, beds, glass,
  stone, stone bricks and TNT.
- Houses: doors that open and close (and block mobs), beds for sleeping
  through the night and setting the respawn point, glass windows.
- Torches (floor and wall) with flood-fill block light.
- Day/night cycle (10 minutes) with sky light, sunrise/sunset colors, sun,
  moon and stars.
- Mobs: pigs and sheep (drop porkchops, wool and mutton), zombies (chase
  and attack, burn in daylight) and creepers (explode and destroy blocks).
- Health, fall damage, drowning, eating, death and respawn.

Not included: saving, sound, caves, flowing water, hunger, item drops on
the ground (broken blocks go straight to the inventory).

## How it works

- `boot/ipl.S`: the CD boot sector ("IPL4").  The TOWNS boot ROM loads it
  to B000:0000 and calls it.  It reads the game through the ROM disk BIOS
  (`FFFB:0014`) into conventional memory and enters 32-bit protected mode.
- `src/`: a freestanding C kernel (gcc `-m32 -march=i386`, no FPU, no C
  library).  It programs the hardware directly: CRTC for 320x240 with 256
  colors, the interrupt controller, interval timer (100 Hz) and VSYNC
  interrupt, the keyboard, the game pad and the mouse.
- Rendering (`render.c`, `raster.c`, `trap.S`): chunk meshes of greedily
  merged, lit face quads; a painter's algorithm without a depth buffer
  using a nested back-to-front order (y slices, then rows, then cells,
  with merged quads drawn at the end of their slice or row); fixed-point
  projection and frustum tests from per-axis tables, with each chunk's
  faces grouped by direction and quadrant so hidden groups are skipped
  whole; a convex polygon
  rasterizer with assembly texture loops.  Frames are drawn straight into
  one of two VRAM pages and shown by changing the display start address.
- Palette: 16 color ramps of 16 shades with a constant brightness ratio,
  so lighting a texel is a subtraction.  Textures are generated at start.

## Building

Requires gcc with `-m32` support, binutils and python3.

```
make            # builds TOWNSCRAFT.ISO
```

## Testing without FM TOWNS ROMs

`tools/stubrom/` contains a **test-only** stub system ROM.  It is not an FM
TOWNS ROM; it only implements power-on, loading the CD boot sector and the
one disk BIOS call the boot sector uses (via the emulated CD-ROM
controller).  It lets the game run in `Tsugaru_Headless` for automated
tests:

```
make stubrom
python3 tests/run_headless.py <path>/Tsugaru_Headless build/STUBROM TOWNSCRAFT.ISO out \
    --shots 5,20 --extra="-TOWNSTYPE MODEL2 -FREQ 16 -MEMSIZE 2 -YESWAIT"
```

`tests/profile.py` symbolizes the sampling profiler buffer dumped from the
VM.  Building with `EXTRA="-DFIXED_SEED=4242 -DBENCH"` creates a benchmark
image: after the world is generated the player looks in 8 fixed directions
for 3 seconds each, and `g_benchFrames` (8 counters) holds the frames drawn
per direction.  Building with `EXTRA="-DTEST_SCENE -DTEST_TIME=3000 -DTEST_DOOR_OPEN=0"`
creates a debug image that places a house, mobs and items in front of the
player.

Because real ROMs were not available while developing, the boot sector was
tested only with the stub ROM.  It follows the calling convention of the
existing boot sector tests in `testdata/IPL` and of TOWNS OS's own boot
sector, but boot with genuine ROMs has not been verified.
