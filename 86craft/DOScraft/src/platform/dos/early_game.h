/* DOS early-game systems: tool wear and the save extension section. */
#ifndef DOSCRAFT_EARLY_GAME_H
#define DOSCRAFT_EARLY_GAME_H
#include "inventory.h"

/* Wears the held tool by cost uses; a worn-out tool breaks. */
void dos_tool_wear(int cost);

/* State that the pinned save layout has no room for, written after the
 * mobs in a large-world save. Load returns 0 on a malformed section. */
void dos_extras_save(void (*put)(u32));
int dos_extras_load(u32 (*get)(void));

#endif
