#ifndef GAME_H
#define GAME_H
#include "common.h"

extern int g_time;          /* 0..23999, 0 = sunrise, 6000 = noon */
extern int g_skyDarken;     /* 0 day .. 11 night */
void game_message(const char *msg);
int game_sky_darken(int time);

#endif
