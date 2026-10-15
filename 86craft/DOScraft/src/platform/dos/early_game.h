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

/* State that the pinned save layout has no room for, written after the
 * mobs in a large-world save. Load returns 0 on a malformed section. */
void dos_extras_save(void (*put)(u32));
int dos_extras_load(u32 (*get)(void));

#endif
