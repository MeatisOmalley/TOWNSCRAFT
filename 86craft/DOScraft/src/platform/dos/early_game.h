/* DOS early-game systems: tool wear and the save extension section. */
#ifndef DOSCRAFT_EARLY_GAME_H
#define DOSCRAFT_EARLY_GAME_H
#include "inventory.h"

/* Wears the held tool by cost uses; a worn-out tool breaks. */
void dos_tool_wear(int cost);

/* What breaking block id (cell b) gives, starting from its usual drop.
 * Gravel gives flint (and tin where the baker marked it); clay gives four
 * lumps (three are added directly). */
int dos_break_drop(int id,u8 b,int drop);

/* Gravel metadata bit set by the baker in tin country. */
#define GRAVEL_TIN 1

/* World edits made by the player (after the cell changes). */
void dos_block_placed(int id,int x,int y,int z);
void dos_block_removed(int id,int x,int y,int z);

/* Campfires: each lit fire burns its own fuel down over game time. */
void dos_fires_tick(void);
void dos_fire_open(int x,int y,int z);     /* Its screen is now open */
void dos_fire_add_fuel(void);              /* F on the campfire screen */
void dos_fire_draw_status(u8 *fb);
const char *dos_station_title(int station);

/* Spears (EARLY_GAME.md, Spear). The jab reaches 2 to 4.5 blocks and
 * returns its cooldown in game ticks; holding use lowers the spear for a
 * charge that hits what the player closes on fast enough. */
extern int dos_use_held;                   /* Use key or button held this frame */
int dos_spear_jab(void);
void dos_spear_tick(void);
int dos_holding_spear(void);

/* Double-tapping forward sprints until it is released, the player walks
 * into a wall or swims (sampled every frame; 100 Hz timer). */
int dos_sprint_tap(int forwardDown);

/* Forget all early-game world state (new game, or before loading). */
void dos_extras_clear(void);

/* State that the pinned save layout has no room for, written after the
 * mobs in a large-world save. Load returns 0 on a malformed section. */
void dos_extras_save(void (*put)(u32));
int dos_extras_load(u32 (*get)(void));

#endif
