#ifndef DOSCRAFT_CRAFT_GRID_H
#define DOSCRAFT_CRAFT_GRID_H

#include "inventory.h"

extern Slot dos_craft_grid[9];

void dos_craft_grid_clear(void);
const Recipe *dos_craft_grid_match(int station,int width);
int dos_craft_grid_take(Slot *destination,int station,int width);
int dos_craft_grid_return(void);

#endif
