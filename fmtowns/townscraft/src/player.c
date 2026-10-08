#include "player.h"
#include "world.h"
#include "fmath.h"
#include "game.h"
#include "sound.h"

Player g_player;
static int jumpHeld;

#define WALK_SPEED 860     /* FU per tick */
/* Gravity and drag apply before movement: 1980 peaks at about 1.24 blocks,
   leaving clearance to land on a one-block ledge. */
#define JUMP_SPEED 1980

void player_init(void)
{
	memset(&g_player,0,sizeof(g_player));
	jumpHeld=0;
	g_player.body.hw=1228;           /* 0.3 blocks */
	g_player.body.h=7373;            /* 1.8 blocks */
	g_player.spawnX=g_spawnX*FU+2048;
	g_player.spawnY=g_spawnY*FU;
	g_player.spawnZ=g_spawnZ*FU+2048;
	player_respawn();
}

void player_respawn(void)
{
	Body *b=&g_player.body;
	int x=g_player.spawnX>>12,z=g_player.spawnZ>>12,y=g_player.spawnY>>12;
	/* Find room: go up until two free cells */
	while(y<WH-2 && (world_is_solid(x,y,z) || world_is_solid(x,y+1,z)))
	{
		++y;
	}
	b->x=g_player.spawnX;
	b->y=y*FU;
	b->z=g_player.spawnZ;
	b->vx=b->vy=b->vz=0;
	b->fallDist=0;
	g_player.health=MAX_HEALTH;
	g_player.dead=0;
	g_player.hurtTimer=0;
	g_player.airTimer=0;
	g_player.pitch=0;
}

void player_hurt(int dmg,int fromX,int fromZ)
{
	Body *b=&g_player.body;
	int dx=b->x-fromX,dz=b->z-fromZ,d;
	if(g_player.dead || g_player.hurtTimer>0 || dmg<=0)
	{
		return;
	}
	g_player.health-=dmg;
	g_player.hurtTimer=10;
	sound_play(SFX_HURT,256,230,0);
	d=isqrt((u32)((dx>>6)*(dx>>6)+(dz>>6)*(dz>>6)))<<6;
	if(d>0)
	{
		b->vx+=dx*1000/d;
		b->vz+=dz*1000/d;
		b->vy=900;
	}
	if(g_player.health<=0)
	{
		g_player.health=0;
		g_player.dead=1;
	}
}

void player_eye(int *x,int *y,int *z)
{
	*x=g_player.body.x;
	*y=g_player.body.y+PLAYER_EYE;
	*z=g_player.body.z;
}

void player_look_dir(int *dx,int *dy,int *dz)
{
	int cp=fcos(g_player.pitch);
	*dx=(fsin(g_player.yaw)*cp)>>14;
	*dy=fsin(g_player.pitch);
	*dz=(fcos(g_player.yaw)*cp)>>14;
}

void player_tick(const PlayerInput *in)
{
	Body *b=&g_player.body;
	int speed=WALK_SPEED,tvx,tvz,sy,cy,wasAir;
	int jumpPressed=in->jump && !jumpHeld;
	jumpHeld=(0!=in->jump);
	if(g_player.dead)
	{
		return;
	}
	if(g_player.hurtTimer>0)
	{
		--g_player.hurtTimer;
	}
	if(g_player.swing>0)
	{
		--g_player.swing;
	}
	if(in->sprint && in->forward>0)
	{
		speed=speed*13/10;
	}
	if(b->inWater)
	{
		speed=speed/2;
	}
	sy=fsin(g_player.yaw);
	cy=fcos(g_player.yaw);
	tvx=((sy*in->forward+cy*in->strafe)*speed)>>14;
	tvz=((cy*in->forward-sy*in->strafe)*speed)>>14;
	if(in->forward && in->strafe)
	{
		tvx=tvx*181/256;
		tvz=tvz*181/256;
	}
	if(b->onGround || b->inWater)
	{
		b->vx+=(tvx-b->vx)/2;
		b->vz+=(tvz-b->vz)/2;
	}
	else
	{
		b->vx+=(tvx-b->vx)/8;
		b->vz+=(tvz-b->vz)/8;
	}
	if(in->jump)
	{
		if(b->inWater)
		{
			b->vy+=260;
			if(b->vy>900) b->vy=900;
			/* Climb out onto a ledge */
			if(b->hitWall)
			{
				b->vy=1300;
			}
		}
		else if(b->onGround && jumpPressed)
		{
			b->vy=JUMP_SPEED;
		}
	}
	wasAir=!b->onGround;
	body_tick(b,GRAVITY);
	/* Fall damage on landing */
	if(b->onGround && wasAir)
	{
		int blocks=b->fallDist/FU;
		if(blocks>3)
		{
			g_player.hurtTimer=0;
			player_hurt(blocks-3,b->x,b->z);
		}
		b->fallDist=0;
	}
	if(b->inWater)
	{
		b->fallDist=0;
	}
	/* Drowning */
	if(b->headInWater)
	{
		if(++g_player.airTimer>200 && 0==(g_player.airTimer%20))
		{
			g_player.hurtTimer=0;
			player_hurt(2,b->x,b->z);
		}
	}
	else
	{
		g_player.airTimer=0;
	}
	/* Regeneration */
	if(g_player.health<MAX_HEALTH && ++g_player.regenTimer>=80)
	{
		g_player.regenTimer=0;
		++g_player.health;
	}
	/* Out of the world */
	if(b->y<-16*FU)
	{
		g_player.health=0;
		g_player.dead=1;
	}
}
