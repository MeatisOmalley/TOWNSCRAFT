#ifndef DOS_VIEWMODEL_H
#define DOS_VIEWMODEL_H
#include "common.h"

/* Presentation only. These values never change inventory, physics or saves. */
void dos_viewmodel_update(int active,int item,int light,int swing,int mining,
                         int walking,int x,int z,u32 ticks);
void dos_viewmodel_draw(int width,int height);
#ifdef DOSCRAFT_BENCH
extern u32 dos_viewmodel_us;
#endif
#endif
