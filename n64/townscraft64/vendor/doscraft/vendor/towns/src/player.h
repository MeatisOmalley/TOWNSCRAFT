#ifndef PLAYER_H
#define PLAYER_H
#include "physics.h"

#define PLAYER_EYE 6636        /* 1.62 blocks */
#define MAX_HEALTH 20          /* Half hearts */

typedef struct
{
	Body body;
	int yaw,pitch;             /* 0..1023 */
	int health;
	int hurtTimer,regenTimer,airTimer;
	int selected;              /* Hotbar slot */
	int dead;
	int spawnX,spawnY,spawnZ;  /* Fine units */
	int hasBedSpawn;
	int swing;                 /* Arm swing animation (ticks) */
} Player;

typedef struct
{
	int forward,strafe;        /* -1..1 */
	int jump,sprint;
} PlayerInput;

extern Player g_player;

void player_init(void);
void player_respawn(void);
void player_tick(const PlayerInput *in);
void player_hurt(int dmg,int fromX,int fromZ);
void player_eye(int *x,int *y,int *z);            /* Fine units */
void player_look_dir(int *dx,int *dy,int *dz);    /* 2.14 unit vector */

#endif
