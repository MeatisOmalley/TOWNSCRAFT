# TOWNSCRAFT

A Minecraft-style block building game for the FM TOWNS, written to run on
the Tsugaru emulator in this repository (and, in principle, on real
hardware).  It boots directly from a CD image, with no TOWNS OS needed.

It is far from Minecraft's feature set, but everything listed under
Features works in the emulator at interactive frame rates.

## Running

The current integrated production build is `TOWNSCRAFT.ISO` in this
directory. Historical numbered builds remain under `iso/`; do not assume
folder 17's older "merged_final" label means the latest source.
The latest numbered launcher is
[run_18_layer_merged.cmd](iso/18_9c11b1c1_layer_merged/run_18_layer_merged.cmd).
See [PERFORMANCE_RESULTS.md](PERFORMANCE_RESULTS.md) for the integration
decision and per-step measurements, and [BENCHMARKING.md](BENCHMARKING.md)
for the standardized level, minimum-spec priority and individual tests.

```
Tsugaru_CUI <ROM directory> -CD TOWNSCRAFT.ISO -BOOTKEY CD -DIFFMOUSE -YESWAIT -AUTOSCALE
```

Saving uses a 1232 KB floppy disk in drive A as a dedicated save disk (its
contents are overwritten).  In Tsugaru add `-FD0 SAVE.BIN`; a blank disk
image can be made with `Tsugaru_CUI -GENFD SAVE.BIN 1232` or as a file of
1261568 zero bytes.  A world can only be loaded on a machine with the same
logical world size. Keep that floppy mounted after saving or loading:
the 96/160/208 maps page clean terrain columns from it during travel.
New unsaved worlds use the dedicated, launcher-prepared SCSI HDD for temporary
terrain backing when available. Without that disk they retain compressed RAM
backing. The HDD archive is not a permanent save: starting a new world replaces
its runtime contents. See [HDD_STREAMING.md](HDD_STREAMING.md).

`-DIFFMOUSE` passes relative mouse motion to the emulated mouse (the game
reads the mouse directly, without the TOWNS OS mouse driver that Tsugaru's
default mouse mode relies on).

`-YESWAIT` keeps the emulator paced to real time.  Without it, the default
unthrottled startup mode can persist with the test ROM, speeding up music
and physics together.

`-AUTOSCALE` resizes the game view to fit the emulator window, preserving
its aspect ratio.  Add `-MAXIMIZE` to start maximized, or `-FULLSCREEN`
to start full screen.  Without auto-scaling, enlarging the window leaves
the game view at its original size.

With the included test ROM (after `make stubrom`), omit the boot key:

```
Tsugaru_CUI build/STUBROM -CD TOWNSCRAFT.ISO -DIFFMOUSE -MEMSIZE 4 -YESWAIT -DONTAUTOSAVECMOS -AUTOSCALE -MAXIMIZE
```

`TOWNSCRAFT.ISO` is prebuilt in this directory. All numbered `.cmd` launchers
now use the expanded **late-1989 minimum target**, rather than the old
2 MB Model 2 baseline:

| Setting | New launcher minimum |
|---|---|
| Machine | Late-1989 2H class, selected as `-TOWNSTYPE 2F` in Tsugaru |
| CPU clock | 386DX, `-FREQ 16` |
| RAM | `-MEMSIZE 8` |
| FPU | Optional 80387 fitted: `-USEFPU` |
| Hard disk | Dedicated 200 MB raw SCSI image per ISO, `-HD0` |
| CD-ROM | Internal single-speed CD, `-CDSPEED 1` |
| Timing | `-NORMALSCSI -NORMALFD -YESWAIT` |

The shared launcher creates a missing HDD under that ISO's `runtime_1989/`
folder. It claims only a completely blank disk or one already marked as our
temporary terrain scratch disk; unknown disk contents are left untouched.
These writable images are
ignored by Git; launchers and the hardware profile are tracked. See
[iso/HARDWARE_1989.md](iso/HARDWARE_1989.md) for sources and limitations.
The current game streams unsaved terrain through its HDD driver and uses a
384 KiB extra-RAM read cache on the 8 MB profile. Permanent saving still uses
the floppy driver. The FPU is enabled but the game remains integer-only.
Historical ISOs do not acquire the new HDD driver merely from these settings.
Existing save floppies
are not mounted, modified or migrated automatically.

The earlier command examples and 2 MB/4 MB/6 MB benchmark results describe
legacy configurations. Benchmark version 1 remains unchanged for historical
comparisons; its `minimum` label still means the old 2 MB profile.

The logical map keeps the existing hardware-profile defaults (96x96 blocks with 2 MB,
160x160 with 4 MB, 208x208 with 6 MB, 256x256 with 8 MB or more; 48 blocks
high). A fixed 25-column cache now holds nearby blocks and light, rather
than allocating those arrays for the whole map. The game picks a render resolution and view
distance from a quick CPU speed check at startup.  Both can be changed in
game (PF2, PF3).  The view distance presets are 8 blocks (base spec),
8/10 and 10/12, plus optional distance presets 6/12 and 6/16. Faces between
the first and second distance are drawn flat in their texture's average
color. The distance presets retain nearby block textures, reject offscreen
faces and patches before projection, and use a faster flat-shading loop.
Startup defaults remain the same.
The 16-block preset is intended for faster machines; it costs considerably
more than the minimum-spec 8-block view.

PF8 toggles optional terrain occlusion culling (off at startup). It skips
fully hidden geometry using nearby opaque faces, while preserving cave
openings and transparent surfaces. It helps most at longer view distances;
rebuilding the proof during movement can offset the gain.

Every frame redraws all rows. Interlaced rendering was removed because
retaining older rows caused visible artifacts during movement.

Earlier measurements before terrain streaming, in Tsugaru with the automatically chosen settings (160x100
rendering of a 320x200 view above the HUD strip), standing at the spawn
point of a fixed test world and looking in 8 directions (see Testing):

| Machine | View distance | Frame rate (average, range) |
|---|---|---|
| Model 2, 386DX 16 MHz, 2 MB | 8 | 15.7 fps, 11-26 fps |
| Tsugaru default profile, 4 MB | 10/12 | 25.1 fps, 20-30 fps |

A recent fixed-seed 2 MB streaming benchmark measured 16.9 FPS against
17.0 FPS before streaming. That difference does not establish a speedup;
the cache primarily provides RAM headroom. Regional mob populations also
change the entity workload, so renderer comparisons must control it.

The default profile runs the CPU at 25 MHz instead of 16.  Tsugaru counts
80486 instruction timings for every machine type, so these numbers are
for a 486 at the given clock; a real 386 needs noticeably more cycles for
the same code, so a real Model 2 would be slower than measured here.

## Controls

| Key | Action |
|---|---|
| W A S D | Move |
| Arrow keys (or numeric keypad 8/4/6/2), mouse | Look |
| SPACE | Jump once per press / hold to swim up |
| CTRL | Sprint |
| J (hold), left mouse button | Break block / attack |
| E, right mouse button | Use (door, bed, crafting table, furnace, TNT), place block, eat |
| 1-9, `,` `.` | Select hotbar slot |
| TAB | Open / close inventory (E or SPACE picks up / places a stack, Q discards) |
| C | Crafting by hand |
| ESC / PF1 | Help |
| PF2 | Toggle 320x200 / 160x100 rendering |
| PF3 | Cycle view presets: 8, 8/10, 10/12, Distance 6/12, Distance 6/16 (textured / total blocks) |
| PF4 | Debug overlay (frame rate, position, time) |
| PF6 | Music on/off |
| PF7 | Sound effects on/off |
| PF8 | Occlusion culling on/off (starts off) |
| PF9 | Save the world to the floppy disk in drive A (L on the title screen loads it) |

A game pad on port A also works: pad to move and turn, A to break/attack,
B to use/place, RUN to jump, SELECT to change the hotbar slot.  The mouse
goes on port B.

## Features

- Island worlds: grass, dirt, sand, gravel, stone with ores,
  bedrock, water, trees, flowers and tall grass. Underground cave networks
  have dry surface entrances with slopes leading down into them.
- Breaking with tool tiers (wood, stone, iron pickaxes, axes, shovels) and
  placing blocks.
- Crafting by hand, at a crafting table and at a furnace (smelting uses
  coal, logs, planks or sticks as fuel).  Recipes include planks, sticks,
  crafting table, torches, furnace, tools, swords, doors, beds, glass,
  stone, stone bricks and TNT.
- Chests (8 planks at a crafting table) holding 27 stacks each; breaking
  a chest moves its contents to the inventory.
- Houses: doors that open and close (and block mobs), beds for sleeping
  through the night and setting the respawn point, glass windows.
- Torches (floor and wall) with flood-fill block light.
- Day/night cycle (10 minutes) with sky light, sunrise/sunset colors, sun,
  moon and stars.
- Weather: drifting clouds; rain spells that dim the light, turn the sky
  grey and bring rain streaks and sound (muffled under a roof).
- Mobs: pigs and sheep (drop porkchops, wool and mutton), zombies (chase
  and attack, burn in daylight) and creepers (explode and destroy blocks).
- Health, fall damage, drowning, eating, death and respawn.
- Sound: effects for digging, breaking and placing (by material),
  footsteps, doors, eating, crafting, damage, creeper and TNT fuses and
  explosions, positioned by distance and direction; and a calm piano
  piece on the FM chip (an original composition) on the title screen and
  every few minutes in game.

Not included: flowing water, hunger, item drops on
the ground (broken blocks go straight to the inventory).

## Work in progress: caves and mesh streaming

Caves, gold, diamonds and diamond tools are in the code. Cave entrances
are generated in new worlds; existing saves retain their original terrain.
Animal populations leave room for zombies and creepers to spawn at night
and on dark cave floors near the player's height. Mesh streaming
builds nearby chunks nearest first, within the view distance plus 8
blocks.  After startup it builds at most one new chunk per frame; pool
pressure can evict farther columns and retry a build.  The pool holds
16000 quads on 2 MB machines and 24000 on larger machines.  Reserving
24000 on a 2 MB machine reduced the world to 80x80; the smaller budget
restores 96x96.  The world is always 48 blocks high, including air above
the terrain. Nearby block/light data now live in the 25-column cache;
the rest use compressed RAM backing or the mounted save disk. Whole-world
generation still happens at startup; this is not unlimited world generation.

Streaming searches only nearby columns and skips the search once the
neighborhood is complete, until the camera cell, radius or meshes change.
Building faces can still cause stutters, and an unusually dense neighborhood
can exceed the pool and have trimmed meshes.

Regression measurement with seed 4242, Model 2, 16 MHz, 2 MB, view distance
8, non-interlaced, using Zig 0.13's Clang for both builds: the streaming
commit measured 10.75 fps at 80x80; the smaller pool and cached search
measured 15.42 fps at 96x96.  The predecessor measured 20.75 fps, but its
exhausted whole-world mesh pool omitted nearby geometry, so it is not an
equal-rendering baseline.  These measurements use the eight-direction
benchmark described below, not real hardware.

## How it works

- `boot/ipl.S`: the CD boot sector ("IPL4").  The TOWNS boot ROM loads it
  to B000:0000 and calls it.  It reads the game through the ROM disk BIOS
  (`FFFB:0014`) into conventional memory and enters 32-bit protected mode.
- `src/`: a freestanding C kernel (gcc `-m32 -march=i386`, no FPU, no C
  library).  Sound (`sound.c`): effects are synthesized at start into the
  RF5c68 PCM wave RAM; the music is an FM electric piano on the YM2612
  driven from the timer interrupt.  It programs the hardware directly: CRTC for 320x240 with 256
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

The stub ROM also works with the windowed `Tsugaru_CUI`.  **Omit
`-BOOTKEY CD` when using the stub ROM**: it boots from CD automatically,
and the simulated boot key otherwise leaves the game stuck draining
keyboard input at startup (a grey window).  The command in Running above
is for a regular FM TOWNS ROM.

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

`tests/stream_test.c` compares streamed mesh contents against the original
exhaustive nearest-first search across 3200 states, including movement,
radius changes, eviction, resets and both pool budgets.  With a native
32-bit C toolchain, run from this directory:

```
python3 tools/gentables.py build/tables.c
gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/stream_test.c src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/stream_test
./build/stream_test
```

`tests/player_test.c` checks one jump per press, a one-block ledge, ceiling
collisions and held-button swimming using the actual player and collision
code.  The ground jump peaks at approximately 1.24 blocks:

```
gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/player_test.c src/player.c src/physics.c src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/player_test
./build/player_test
```

`tests/keyboard_test.c` checks the actual keyboard packet decoder together
with player physics: three seconds of uninterrupted walking through
typematic repeat packets, simultaneous held keys and real releases.
`tests/mobs_test.c` checks animal/hostile capacity, daylight, night, torch
light and cave-floor spawning at every supported world size.
`tests/caves_test.c` checks walkable access from spawn into caves across
16 generated worlds, safe spawns, sealed ocean floors, bedrock and mesh
budgets. With the same native 32-bit toolchain:

```
gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/keyboard_test.c src/player.c src/physics.c src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/keyboard_test
./build/keyboard_test
gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/mobs_test.c src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/mobs_test
./build/mobs_test
gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/caves_test.c src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/caves_test
./build/caves_test
```

`tests/mob_regions_test.c` checks exact dormant state, ownership, stable IDs,
empty populations, slot pressure and persistence. `tests/column_cache_test.c`
compares resident blocks and lighting against full-world references through
travel, prefetch, edit eviction and corruption/full-store failures.
`tests/column_save_test.c` exercises the actual floppy driver with a DMA
fixture, including interrupted bank writes, damaged-bank fallback, runtime
read failures, old-save import and 256-wide save compatibility. Build them
with the same native command as `mobs_test.c` above.

`tests/render_occlusion_test.c` compares all viewport pixels with downward
floor culling disabled over 960 views: opaque floors, cliffs, cave openings,
glass, water, both render scales, and entities. It also checks unfinished
meshes, pending edits and missing terrain. On a native 32-bit Linux host:

```sh
gcc -m32 -O2 -ffreestanding -fno-builtin -Isrc tests/render_occlusion_test.c src/raster.c src/trap.S src/blocks.c src/fmath.c src/libc.c build/tables.c -o build/render_occlusion_test
./build/render_occlusion_test
```

See [STREAMING.md](STREAMING.md) for the implemented cache, mob regions,
disk paging, current limits and the next generation/storage design.

Because real ROMs were not available while developing, the boot sector was
tested only with the stub ROM.  It follows the calling convention of the
existing boot sector tests in `testdata/IPL` and of TOWNS OS's own boot
sector, but boot with genuine ROMs has not been verified.
