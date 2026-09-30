#ifndef MOBS_H
#define MOBS_H
#include "physics.h"

enum
{
	MOB_NONE,MOB_PIG,MOB_SHEEP,MOB_ZOMBIE,MOB_CREEPER,MOB_TNT,
	NUM_MOB_TYPES
};

typedef struct
{
	u8 type;
	s16 health;
	Body body;
	int yaw,targetYaw;
	int aiTimer,walking;
	int hurtTimer,attackTimer,panic,fuse,burnTimer;
	int walkPhase;
} Mob;

#define MAX_MOBS 24
extern Mob g_mobs[MAX_MOBS];

int mob_spawn(int type,int x,int y,int z);          /* Fine units; returns index or -1 */
void mobs_clear(void);
void mobs_tick(int skyDarken);
void mobs_add_render_boxes(int skyDarken);
int mobs_ray_hit(int ex,int ey,int ez,int dx,int dy,int dz,int maxDist,int *dist);
void mob_hurt(int i,int dmg,int fromX,int fromZ,int byPlayer);
void explode(int x,int y,int z,int power);
int mobs_hostiles_near(int x,int y,int z,int radiusBlocks);
void mobs_spawn_initial(void);
void mobs_try_spawn(int skyDarken);
int mobs_count(int hostile);
int is_hostile(int type);

#endif
