# Townscraft64 feature checklist (DOScraft parity)

Reference: DOScraft at `c4ce3b7b` (re-pinned at N2b), imported read-only into
[vendor/doscraft](vendor/doscraft/MANIFEST.json) (Q1). Phase A parity (N7)
means every row below is done or explicitly waived by the user.

## How DOScraft is built

DOScraft is not a fork of the Towns code. Its game source is assembled at
build time from three layers:

1. **Towns core**: `vendor/doscraft/vendor/towns/src`, the immutable Towns
   snapshot `d706a67d` (50 files, own manifest).
2. **Overlays**: `vendor/doscraft/tools/dos_*.py`. Each one applies exact,
   fail-closed text replacements to Towns sources (`dos_sources.adapt`).
   Many DOScraft *game features* live here, not in separate files.
3. **DOS platform code**: `vendor/doscraft/src/platform/dos/*`, which mixes
   VGA/IRQ/Sound Blaster drivers with portable feature code (menus, craft
   grid, viewmodel, large-world streaming, audio voices).

The shipped game uses `DEFAULT_ADAPTER_FLAGS` from `tools/build_game.py`
plus `-DDOS_LARGE_WORLD -DDOS_STREAM_LIGHT_CACHE -DDOS_FAR_FIELD
-DDOS_FAR_DETAILS -DDOS_FAR_VIEW_DEPTH -DDOS_FAR_REPROJECT`, with graphics
presets and audio on (as in `tools/prepare_386_vm.py`).

Tags:

- **core**: Towns core code compiled for the N64 as-is (plus endian and
  alignment fixes).
- **extract**: portable logic from a DOScraft overlay or platform file,
  moved into shared N64 source and tested against DOScraft's behavior.
- **reimplement**: the design carries over; the N64 code is new (hardware,
  renderer, storage).
- **drop**: PC- or 486-specific; no N64 equivalent needed.

## Gameplay and world

| Feature | Source | Tag | Notes |
| --- | --- | --- | --- |
| Blocks, tool tiers, breaking/placing | `blocks.c`, `game.c` | core | |
| 6-bit block IDs; wall torches and open doors as their own blocks | `dos_block_ids.py` | extract | Changes the one-byte cell layout (id bits 0-5, metadata 6-7) and so the world bake. |
| Inventory, hotbar, chests (27 stacks) | `inventory.c`, `game.c` | core | |
| Crafting by hand, crafting table, furnace/smelting/fuel | `game.c`, `ui.c` | core | |
| Combined inventory/crafting menu | `dos_crafting.py`, `craft_grid.c` | extract | DOScraft UI over the Towns rules. |
| Doors, beds (sleep, respawn), glass, TNT | `game.c`, `world.c` | core | |
| Torches, flood-fill block light | `world.c` | core | |
| Immediate edit lighting while streaming | `dos_immediate_edit_light.py` | extract | |
| Replaceable-plant placement fix | `dos_plant_placement.py` | extract | Bug fix. |
| Day/night, sky light, sunrise/sunset, sun/moon/stars | `game.c`, `render.c` | core | Drawing reimplemented with the renderer. |
| 20-minute day | `dos_day_length.py` | extract | Towns is 10 minutes. |
| Weather: clouds, rain, dimmed light, rain sound | `game.c`, `render.c` | core | |
| Player physics, collision, jump, swim, sprint, fall damage, drowning | `player.c`, `physics.c` | core | Host tests: `player_test`, `keyboard_test`. |
| Health, eating, death, respawn | `game.c`, `player.c` | core | |
| Mobs: pig, sheep, zombie (burns in daylight), creeper (explodes) | `mobs.c` | core | Host test: `mobs_test`. |
| Chickens (model, AI, drops, raw/cooked chicken, feathers, mob ID 6, items 59-61) | `dos_chickens.py`, `CHICKENS.md` | extract | |
| Mob despawn beyond 32 blocks | `dos_mob_despawn.py` | extract | |
| Regional mob populations, dormant state | `mobs.c` | core | Host test: `mob_regions_test`. |
| Island worlds, caves, ores, trees, plants (256 world) | `world.c` | core | Host test: `caves_test`. |
| 1024x1024 prebaked world (seed 4242, crown leaves) | `bake_large_world.py`, `bake_tree_canopies.py`, `large_world.inc` | extract | Bake tools run on the host; the world goes into ROM (Q6). |
| Biome world (plains, forests, deserts, highlands) | `terrain_biomes.py`, `BIOMES_AND_TEXTURES.md` | extract | Offline bake. Currently a preview world in DOScraft; confirm whether it is the N64 default. |
| Baked skylight for prebaked columns | `bake_large_light.py`, `dos_stream_light.py`, `stream_light_*` | extract | |
| Column cache (25 resident columns) | `column_cache.inc` | core | Host tests: `column_cache_test`, `stream_test`. |
| Terrain backing (HDD scratch, V2/V3 media) | `hdd.c`, `column_hdd.inc`, `hdd.c` (DOS) | reimplement | ROM base + RAM edit store (Q6). |
| Saves: large-world save version 6, V3 edit log, two banks | `save.c` (DOS), `LARGE_WORLD_STORAGE.md` | reimplement | FlashRAM block-level edits (Q6); 64DD path at N8. |
| Single save slot, New Game / Load Game semantics | `dos_menus.py`, `menus_*.inc` | extract | |

## Rendering and presentation

| Feature | Source | Tag | Notes |
| --- | --- | --- | --- |
| Greedy-merged chunk meshes, per-face light | `render.c` | core | Mesh building reused; drawing goes to the RDP. |
| Frustum, backface, group and floor culling | `render.c` | core | |
| Painter sort, affine subdivision, span rasterizer, `trap.S` | `render.c`, `raster.c`, `trap.S` | drop | RDP draws with Z-buffer and perspective. |
| All 486/VGA overlays (`dos_trap_*`, `dos_row_*`, `dos_*planar*`, `dos_texel_edx`, `dos_proven_rows`, `dos_raster_finish_skip`, `dos_mulshift`, `dos_memmove_dwords`, `dos_fill_rows`, `dos_flat_inline`, `dos_sort_histograms`, `dos_collect_*`, `dos_group_facing`, `dos_wall_visibility`, `dos_split_reuse`, `dos_depth_adaptive`, `dos_front_to_back`, `dos_potato`, `dos_very_low_*`, `dos_next4`) | `tools/dos_*.py` | drop | CPU-renderer tuning. Their findings may inform Phase B. |
| Mode X presenter, HUD caching, page flips | `video.c`, `vga_*.c` | drop | libdragon display. |
| Textured distance, flat-shaded distance, total view distance | `dos_flat_distance.py`, `graphics_presets.c` | extract | Settings carry over; the cost model is different on the RDP. |
| Graphics presets Low/Medium/High/Very Low, Custom, F2 cycling | `dos_graphics.py`, `graphics_presets.c` | reimplement | Presets must be re-tuned for N64 costs. |
| Resolution-selected block textures (simple set for small views, Eaglercraft-derived set for full resolution, custom grass top) | `dos_block_textures.py`, `assets/textures` | extract | Formats change for TMEM (CI4/CI8). |
| Plant billboards (Very Low) | `dos_plant_billboards.py` | reimplement | Whether a billboard tier is needed on the RDP is an N3 question. |
| Distant terrain Off/Very Low/Low/Medium/High (heightmap band and panorama) | `dos_far_field.py`, `far_*.inc`, `bake_far_field.py`, `DISTANT_TERRAIN.md` | reimplement | Distant map data extracted; drawing redesigned for the RDP. |
| First-person arm, held blocks and items | `dos_viewmodel.py`, `viewmodel.c` | extract | Model data and animation extracted; drawing reimplemented. |
| HUD: hotbar, health, crosshair, debug overlay | `ui.c`, `gfx.c`, `font.c` | core | Drawing goes through rdpq. |

## Menus and controls

| Feature | Source | Tag | Notes |
| --- | --- | --- | --- |
| Main menu (New Game, Load Game, Settings, Quit) | `dos_menus.py`, `menus_draw.inc`, `menus_input.inc` | extract | "Quit to DOS" has no N64 meaning; replace or drop. |
| Pause menu (Resume, Settings, Save, Save & Main Menu) | same | extract | |
| Settings submenus: Graphics, Controls, Diagnostics | same | extract | Controls gains the controller fallback mapping (Q5). |
| Keyboard controls (WASD, arrows, J/E, Tab, C, 1-9, F1/F2/F6/F7/F9) | `game.c`, `keyboard.c` | reimplement | Randnet keyboard driver (needs ares fork). |
| Mouse look, left/right buttons | `game.c`, `input.c` | reimplement | N64 Mouse via libdragon (verified at N1). |
| Controller-only fallback (Turok-style) | none | reimplement | New for N64 (Q5). |

## Audio

| Feature | Source | Tag | Notes |
| --- | --- | --- | --- |
| Original ten synthesized effects | `sound.c`, `sound_assets.c` | extract | Portable PCM generation already split out by DOScraft. |
| Hand-picked sampled effects (dig/break/place per material, footsteps, doors, hits, mobs, water, zombie groans) | `assets/audio`, `build_sound_bank.py`, `audio_bank.c`, `game_audio.c`, `dos_game_audio.py` | extract | Event selection is portable; resample for the N64 mixer. |
| Positional volume by distance and direction | `game_audio.c`, `audio_voices.c` | extract | N64 mixer is stereo. |
| Music: original piano piece plus MIDI songs, per-song settings, Minecraft-style spacing, master level | `opl_music.c`, `build_music_pack.py`, `assets/music` | reimplement | Sequencer on the libdragon mixer with sampled instruments; song data and settings extracted. |
| F6 music / F7 effects toggles | `game.c` | core | |

## Planned in DOScraft (not yet implemented)

| Feature | Source | Notes |
| --- | --- | --- |
| Early game: clay, copper, bronze, spear, hunger | `EARLY_GAME.md` | Design only at `c4ce3b7b`. Track as DOScraft implements it. |

## Diagnostics and tests

| Item | Source | Tag | Notes |
| --- | --- | --- | --- |
| Towns host tests for code the N64 runs unchanged: player, mobs, mob regions | `vendor/towns-tests` (Towns `d706a67d`) | core | Pass big-endian in ares (N2). Other Towns tests cover generation, the 256-world path, or replaced hardware; see the N2 report. |
| DOScraft tests: chickens, mob codec (7 cases), immediate edit light | `vendor/doscraft/tests` | extract | Pass big-endian in ares (N2b). |
| DOScraft tests needing the baked world: stream light, far world | `vendor/doscraft/tests` | extract | Need an N64 sector backend, a current bake and a byte-order decision; see the N2b report. |
| DOScraft native tests | `86craft/DOScraft/tests` (not imported) | extract | Bring individual tests over with the feature they cover. |
| Benchmark routes, frame-time reports | `bench.c`, `tools/benchmark*.py` | reimplement | N64 profiler and ares-based runs. |
