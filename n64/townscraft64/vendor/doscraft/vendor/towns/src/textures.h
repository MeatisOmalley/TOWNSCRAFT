#ifndef TEXTURES_H
#define TEXTURES_H
#include "blocks.h"

enum
{
	R_GRAY,R_DIRT,R_GRASS,R_LEAF,R_SAND,R_BARK,R_PLANK,R_WATER,
	R_SKY,R_RED,R_PINK,R_FLAME,R_CYAN,R_BLUE,R_PEACH,R_OLIVE
};
#define P(ramp,shade) ((ramp)*16+(shade))

/* Common UI colors */
#define C_BLACK  P(R_GRAY,0)
#define C_DGRAY  P(R_GRAY,7)
#define C_GRAY   P(R_GRAY,10)
#define C_LGRAY  P(R_GRAY,12)
#define C_WHITE  P(R_GRAY,15)
#define C_YELLOW P(R_FLAME,15)
#define C_RED    P(R_RED,13)

#define NUM_SHADED_TEXTURES T_I_STICK

extern u8 g_tex[NUM_TEXTURES][256];
extern u8 *g_texAtlas;
#define TEX_TILE(t,L) (g_texAtlas+(t)*4096+(L)*16)
extern u8 g_shadeLUT[16][256];

void textures_init(void);
void palette_apply(void);
u8 texture_average(int id);

#endif
