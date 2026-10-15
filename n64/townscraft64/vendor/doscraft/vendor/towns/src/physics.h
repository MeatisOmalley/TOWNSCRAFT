/* Entity physics.  Positions are in fine units: 4096 per block. */
#ifndef PHYSICS_H
#define PHYSICS_H
#include "common.h"

#define FU 4096
#define FU_TO_RU(v) ((v)>>4)     /* Fine units to render units (256 per block) */

typedef struct
{
	int x,y,z;          /* Feet center */
	int vx,vy,vz;       /* Fine units per tick */
	int hw,h;           /* Half width, height */
	u8 onGround,inWater,hitWall,headInWater;
	int fallDist;       /* Accumulated fall, fine units */
} Body;

void body_tick(Body *b,int gravity);    /* Apply velocity with collision */
int aabb_hits_world(int x0,int y0,int z0,int x1,int y1,int z1);
int body_touches_block(const Body *b,int bx,int by,int bz);

#define GRAVITY 300       /* FU per tick^2 (20 ticks/s) */
#define TICKS_PER_GAME_SEC 20

#endif
