/* Mobs: passive pigs and sheep, hostile zombies and creepers, and primed
   TNT (which uses the same physics). */
#include "mobs.h"
#include "player.h"
#include "world.h"
#include "render.h"
#include "fmath.h"
#include "inventory.h"
#include "textures.h"
#include "game.h"
#include "sys.h"
#include "sound.h"

Mob g_mobs[MAX_MOBS];

typedef struct
{
	int hw,h,health,speed;
} MobStats;

static const MobStats stats[NUM_MOB_TYPES]=
{
	{0,0,0,0},
	{1843,3686,10,300},   /* Pig */
	{1843,5325,8,300},    /* Sheep */
	{1228,7987,20,390},   /* Zombie */
	{1228,6963,20,360},   /* Creeper */
	{2000,4000,1,0},      /* TNT */
};

/* Model parts in 1/16 block, facing +Z.  anim: 0 none, 1/2 legs (opposite
   phase), 3 arms held forward. */
typedef struct
{
	s8 px,py,pz;
	s8 x0,y0,z0,x1,y1,z1;
	u8 anim;
	u8 side,front,top;   /* textures: sides/back/bottom, +Z, +Y */
} Part;

#define LEG(cx,cz,h,a,t) {cx,h,cz, cx-2,0,cz-2,cx+2,h,cz+2, a, t,t,t}

static const Part pigParts[]=
{
	{0,10,0, -5,6,-8,5,14,8, 0, T_PIG_SKIN,T_PIG_SKIN,T_PIG_SKIN},
	{0,12,8, -4,8,8,4,16,15, 0, T_PIG_SKIN,T_PIG_FACE,T_PIG_SKIN},
	LEG(-3,5,6,1,T_PIG_SKIN),LEG(3,5,6,2,T_PIG_SKIN),LEG(-3,-5,6,2,T_PIG_SKIN),LEG(3,-5,6,1,T_PIG_SKIN),
};
static const Part sheepParts[]=
{
	{0,12,0, -5,7,-8,5,17,8, 0, T_SHEEP_WOOL,T_SHEEP_WOOL,T_SHEEP_WOOL},
	{0,14,8, -3,12,7,3,19,13, 0, T_SHEEP_WOOL,T_SHEEP_FACE,T_SHEEP_WOOL},
	LEG(-3,5,7,1,T_PLAYER_SKIN),LEG(3,5,7,2,T_PLAYER_SKIN),LEG(-3,-5,7,2,T_PLAYER_SKIN),LEG(3,-5,7,1,T_PLAYER_SKIN),
};
static const Part zombieParts[]=
{
	{-2,12,0, -4,0,-2,0,12,2, 1, T_PANTS,T_PANTS,T_PANTS},
	{2,12,0, 0,0,-2,4,12,2, 2, T_PANTS,T_PANTS,T_PANTS},
	{0,18,0, -4,12,-2,4,24,2, 0, T_SHIRT,T_SHIRT,T_SHIRT},
	{-6,22,0, -8,12,-2,-4,24,2, 3, T_ZOMBIE_SKIN,T_ZOMBIE_SKIN,T_ZOMBIE_SKIN},
	{6,22,0, 4,12,-2,8,24,2, 3, T_ZOMBIE_SKIN,T_ZOMBIE_SKIN,T_ZOMBIE_SKIN},
	{0,24,0, -4,24,-4,4,32,4, 0, T_ZOMBIE_SKIN,T_ZOMBIE_FACE,T_ZOMBIE_SKIN},
};
static const Part creeperParts[]=
{
	LEG(-2,4,6,1,T_CREEPER_SKIN),LEG(2,4,6,2,T_CREEPER_SKIN),LEG(-2,-4,6,2,T_CREEPER_SKIN),LEG(2,-4,6,1,T_CREEPER_SKIN),
	{0,12,0, -4,6,-2,4,18,2, 0, T_CREEPER_SKIN,T_CREEPER_SKIN,T_CREEPER_SKIN},
	{0,18,0, -4,18,-4,4,26,4, 0, T_CREEPER_SKIN,T_CREEPER_FACE,T_CREEPER_SKIN},
};
static const Part tntParts[]=
{
	{0,8,0, -8,0,-8,8,16,8, 0, T_TNT_SIDE,T_TNT_SIDE,T_TNT_TOP},
};

int is_hostile(int type)
{
	return MOB_ZOMBIE==type || MOB_CREEPER==type;
}

void mobs_clear(void)
{
	memset(g_mobs,0,sizeof(g_mobs));
}

int mob_spawn(int type,int x,int y,int z)
{
	int i;
	for(i=0; i<MAX_MOBS; ++i)
	{
		if(MOB_NONE==g_mobs[i].type)
		{
			Mob *m=&g_mobs[i];
			memset(m,0,sizeof(*m));
			m->type=type;
			m->health=stats[type].health;
			m->body.x=x;
			m->body.y=y;
			m->body.z=z;
			m->body.hw=stats[type].hw;
			m->body.h=stats[type].h;
			m->yaw=m->targetYaw=rnd_range(1024);
			if(MOB_TNT==type)
			{
				m->fuse=60;
				m->body.vy=800;
				sound_play_at(SFX_HISS,256,255,x,y,z);
			}
			return i;
		}
	}
	return -1;
}

int mobs_count(int hostile)
{
	int i,n=0;
	for(i=0; i<MAX_MOBS; ++i)
	{
		int t=g_mobs[i].type;
		if(t!=MOB_NONE && t!=MOB_TNT && is_hostile(t)==hostile)
		{
			++n;
		}
	}
	return n;
}

static void drops(int type)
{
	switch(type)
	{
	case MOB_PIG:
		inv_add(I_RAW_PORK,1+rnd_range(2));
		game_message("+ Raw Porkchop");
		break;
	case MOB_SHEEP:
		inv_add(B_WOOL,1+rnd_range(2));
		inv_add(I_RAW_MUTTON,1);
		game_message("+ Wool, Raw Mutton");
		break;
	case MOB_ZOMBIE:
		if(rnd_range(3))
		{
			inv_add(I_ROTTEN_FLESH,1+rnd_range(2));
			game_message("+ Rotten Flesh");
		}
		break;
	case MOB_CREEPER:
		inv_add(I_GUNPOWDER,1+rnd_range(2));
		game_message("+ Gunpowder");
		break;
	}
}

void mob_hurt(int i,int dmg,int fromX,int fromZ,int byPlayer)
{
	Mob *m=&g_mobs[i];
	int dx,dz,d;
	if(MOB_NONE==m->type || MOB_TNT==m->type || m->hurtTimer>0)
	{
		return;
	}
	m->health-=dmg;
	m->hurtTimer=10;
	{
		/* Voices: pig and sheep high, zombie low */
		static const u16 hurtPitch[]={256,420,480,170,300,256};
		sound_play_at(SFX_HURT,hurtPitch[m->type<ARRAY_LEN(hurtPitch) ? m->type : 0],220,m->body.x,m->body.y+FU,m->body.z);
	}
	dx=m->body.x-fromX;
	dz=m->body.z-fromZ;
	d=isqrt(dx*(dx>>8)+dz*(dz>>8))<<4;
	if(d>0)
	{
		m->body.vx=dx*1400/d;
		m->body.vz=dz*1400/d;
	}
	m->body.vy=1100;
	if(!is_hostile(m->type))
	{
		m->panic=100;
	}
	if(m->health<=0)
	{
		if(byPlayer)
		{
			drops(m->type);
		}
		m->type=MOB_NONE;
	}
}

void explode(int x,int y,int z,int power)
{
	int bx=x>>12,by=y>>12,bz=z>>12,dx,dy,dz,i;
	int r2=power*power;
	sound_play_at(SFX_EXPLODE,256,255,x,y,z);
	for(dy=-power; dy<=power; ++dy)
	{
		for(dz=-power; dz<=power; ++dz)
		{
			for(dx=-power; dx<=power; ++dx)
			{
				int d2=dx*dx+dy*dy+dz*dz;
				u8 b;
				if(d2>r2+rnd_range(power+1))
				{
					continue;
				}
				b=wget(bx+dx,by+dy,bz+dz);
				switch(BLK_ID(b))
				{
				case B_AIR:
				case B_WATER:
				case B_BEDROCK:
					break;
				case B_TNT:
					world_set(bx+dx,by+dy,bz+dz,B_AIR);
					i=mob_spawn(MOB_TNT,(bx+dx)*FU+2048,(by+dy)*FU,(bz+dz)*FU+2048);
					if(i>=0)
					{
						g_mobs[i].fuse=10+rnd_range(20);
					}
					break;
				default:
					if(in_world(bx+dx,by+dy,bz+dz))
					{
						if(B_CHEST==BLK_ID(b))
						{
							chest_remove(bx+dx,by+dy,bz+dz,0);   /* Contents are lost */
						}
						world_set(bx+dx,by+dy,bz+dz,B_AIR);
					}
					break;
				}
			}
		}
	}
	/* Damage by distance */
	{
		int reach=power*2*FU,maxDmg=power*6;
		int px=g_player.body.x,py=g_player.body.y+FU,pz=g_player.body.z;
		int d=(int)isqrt((u32)(((px-x)>>6)*((px-x)>>6)+((py-y)>>6)*((py-y)>>6)+((pz-z)>>6)*((pz-z)>>6)))<<6;
		if(d<reach && !g_player.dead)
		{
			player_hurt(maxDmg*(reach-d)/reach+1,x,z);
		}
		for(i=0; i<MAX_MOBS; ++i)
		{
			Mob *m=&g_mobs[i];
			if(MOB_NONE==m->type || MOB_TNT==m->type)
			{
				continue;
			}
			px=m->body.x; py=m->body.y+FU/2; pz=m->body.z;
			d=(int)isqrt((u32)(((px-x)>>6)*((px-x)>>6)+((py-y)>>6)*((py-y)>>6)+((pz-z)>>6)*((pz-z)>>6)))<<6;
			if(d<reach)
			{
				mob_hurt(i,maxDmg*(reach-d)/reach+1,x,z,0);
			}
		}
	}
}

static int turn_toward(int yaw,int target,int maxStep)
{
	int d=((target-yaw+512)&1023)-512;
	if(d>maxStep) d=maxStep;
	if(d<-maxStep) d=-maxStep;
	return (yaw+d)&1023;
}

static int dist_blocks_to_player(const Mob *m,int *dx,int *dz,int *dy)
{
	*dx=g_player.body.x-m->body.x;
	*dz=g_player.body.z-m->body.z;
	*dy=g_player.body.y-m->body.y;
	return (int)isqrt((u32)((*dx>>6)*(*dx>>6)+(*dz>>6)*(*dz>>6)))>>6;
}

static void mob_ai(Mob *m,int skyDarken)
{
	int speed=stats[m->type].speed,dx,dz,dy,dist;
	int chase=0;
	dist=dist_blocks_to_player(m,&dx,&dz,&dy);
	switch(m->type)
	{
	case MOB_ZOMBIE:
		if(!g_player.dead && dist<16)
		{
			chase=1;
			m->targetYaw=fatan2(dx,dz);
			if(dist<=1 && ABS(dy)<FU*3/2 && 0==m->attackTimer)
			{
				player_hurt(3,m->body.x,m->body.z);
				m->attackTimer=20;
			}
		}
		/* Burn in daylight under open sky */
		if(skyDarken<4 && !m->body.inWater)
		{
			int L=world_light_at(m->body.x>>12,(m->body.y+m->body.h-1)>>12,m->body.z>>12);
			if((L>>4)>=15)
			{
				if(++m->burnTimer>=20)
				{
					m->burnTimer=0;
					m->hurtTimer=0;
					mob_hurt((int)(m-g_mobs),2,m->body.x,m->body.z,0);
					if(MOB_NONE==m->type)
					{
						return;
					}
				}
			}
		}
		break;
	case MOB_CREEPER:
		if(!g_player.dead && dist<16)
		{
			chase=1;
			m->targetYaw=fatan2(dx,dz);
			if(dist<3)
			{
				chase=0;
				if(0==m->fuse)
				{
					sound_play_at(SFX_HISS,256,255,m->body.x,m->body.y+FU,m->body.z);
				}
				++m->fuse;
				if(m->fuse>=30)
				{
					int x=m->body.x,y=m->body.y+FU/2,z=m->body.z;
					m->type=MOB_NONE;
					explode(x,y,z,3);
					return;
				}
			}
			else if(m->fuse>0 && dist>5)
			{
				--m->fuse;
			}
		}
		else if(m->fuse>0)
		{
			--m->fuse;
		}
		break;
	}
	if(m->panic>0)
	{
		--m->panic;
		chase=1;
		speed=speed*8/5;
		if(0==(m->panic&15))
		{
			m->targetYaw=(fatan2(-dx,-dz)+rnd_range(256)-128)&1023;
		}
	}
	if(!chase)
	{
		if(--m->aiTimer<=0)
		{
			m->walking=(rnd_range(10)<6);
			m->aiTimer=40+rnd_range(80);
			if(m->walking)
			{
				m->targetYaw=rnd_range(1024);
			}
		}
	}
	else
	{
		m->walking=1;
	}
	m->yaw=turn_toward(m->yaw,m->targetYaw,chase ? 96 : 40);
	if(m->walking && m->hurtTimer<5)
	{
		int tvx=(fsin(m->yaw)*speed)>>14,tvz=(fcos(m->yaw)*speed)>>14;
		m->body.vx+=(tvx-m->body.vx)/2;
		m->body.vz+=(tvz-m->body.vz)/2;
		m->walkPhase+=speed/8;
		/* Hop over one-block obstacles */
		if(m->body.hitWall && m->body.onGround)
		{
			m->body.vy=1720;
		}
	}
	else
	{
		m->body.vx=m->body.vx*3/5;
		m->body.vz=m->body.vz*3/5;
	}
	if(m->body.inWater)
	{
		m->body.vy+=380;
	}
}

void mobs_tick(int skyDarken)
{
	static u32 tickCount;
	int i;
	++tickCount;
	for(i=0; i<MAX_MOBS; ++i)
	{
		Mob *m=&g_mobs[i];
		if(MOB_NONE==m->type)
		{
			continue;
		}
		if(m->hurtTimer>0) --m->hurtTimer;
		if(m->attackTimer>0) --m->attackTimer;
		if(MOB_TNT==m->type)
		{
			m->body.vx=m->body.vx*3/4;
			m->body.vz=m->body.vz*3/4;
			body_tick(&m->body,GRAVITY);
			if(--m->fuse<=0)
			{
				int x=m->body.x,y=m->body.y+FU/2,z=m->body.z;
				m->type=MOB_NONE;
				explode(x,y,z,4);
			}
			continue;
		}
		/* Mobs far from the player think and move at a quarter of the rate */
		{
			int dx=(g_player.body.x-m->body.x)>>12,dz=(g_player.body.z-m->body.z)>>12;
			if(dx*dx+dz*dz>24*24 && ((tickCount+i)&3))
			{
				continue;
			}
		}
		mob_ai(m,skyDarken);
		if(MOB_NONE==m->type)
		{
			continue;
		}
		body_tick(&m->body,GRAVITY);
		m->body.fallDist=0;
		if(m->body.y<-8*FU)
		{
			m->type=MOB_NONE;
			continue;
		}
		/* Hostiles far away despawn */
		if(is_hostile(m->type))
		{
			int dx=(g_player.body.x-m->body.x)>>12,dz=(g_player.body.z-m->body.z)>>12;
			if(dx*dx+dz*dz>48*48)
			{
				m->type=MOB_NONE;
			}
		}
	}
}

void mobs_add_render_boxes(int skyDarken)
{
	int i,k;
	for(i=0; i<MAX_MOBS; ++i)
	{
		const Mob *m=&g_mobs[i];
		const Part *parts;
		int nParts,legSwing,L,flash;
		switch(m->type)
		{
		case MOB_PIG:     parts=pigParts;     nParts=ARRAY_LEN(pigParts);     break;
		case MOB_SHEEP:   parts=sheepParts;   nParts=ARRAY_LEN(sheepParts);   break;
		case MOB_ZOMBIE:  parts=zombieParts;  nParts=ARRAY_LEN(zombieParts);  break;
		case MOB_CREEPER: parts=creeperParts; nParts=ARRAY_LEN(creeperParts); break;
		case MOB_TNT:     parts=tntParts;     nParts=ARRAY_LEN(tntParts);     break;
		default: continue;
		}
		legSwing=m->walking ? (fsin(m->walkPhase)*90)>>14 : 0;
		L=face_light_level(world_light_at(m->body.x>>12,(m->body.y+2048)>>12,m->body.z>>12),DIR_PY,skyDarken);
		flash=(m->hurtTimer>5) || (MOB_CREEPER==m->type && m->fuse>0 && (m->fuse&4)) || (MOB_TNT==m->type && (m->fuse&8));
		for(k=0; k<nParts; ++k)
		{
			const Part *p=&parts[k];
			MBox *b=render_add_box();
			int d;
			if(!b)
			{
				return;
			}
			b->ox=FU_TO_RU(m->body.x);
			b->oy=FU_TO_RU(m->body.y);
			b->oz=FU_TO_RU(m->body.z);
			b->yaw=m->yaw;
			switch(p->anim)
			{
			case 1: b->pitch=legSwing; break;
			case 2: b->pitch=-legSwing; break;
			case 3: b->pitch=-240+legSwing/4; break;
			default: b->pitch=0; break;
			}
			b->px=p->px; b->py=p->py; b->pz=p->pz;
			b->x0=p->x0; b->y0=p->y0; b->z0=p->z0;
			b->x1=p->x1; b->y1=p->y1; b->z1=p->z1;
			for(d=0; d<6; ++d)
			{
				b->tex[d]=p->side;
			}
			b->tex[DIR_PZ]=p->front;
			b->tex[DIR_PY]=p->top;
			if(MOB_TNT==m->type)
			{
				b->tex[DIR_NY]=p->top;
			}
			b->light=MAX(L,3);
			b->flash=flash;
			b->hw=FU_TO_RU(m->body.hw);
			b->h=FU_TO_RU(m->body.h);
		}
	}
}

int mobs_ray_hit(int ex,int ey,int ez,int dx,int dy,int dz,int maxDist,int *dist)
{
	int i,best=-1,bestT=maxDist;
	for(i=0; i<MAX_MOBS; ++i)
	{
		const Mob *m=&g_mobs[i];
		int lo[3],hi[3],e[3],d[3],tmin=0,tmax=bestT,k;
		if(MOB_NONE==m->type || MOB_TNT==m->type)
		{
			continue;
		}
		if(ABS(m->body.x-ex)>maxDist+FU || ABS(m->body.z-ez)>maxDist+FU)
		{
			continue;
		}
		lo[0]=m->body.x-m->body.hw; hi[0]=m->body.x+m->body.hw;
		lo[1]=m->body.y;            hi[1]=m->body.y+m->body.h;
		lo[2]=m->body.z-m->body.hw; hi[2]=m->body.z+m->body.hw;
		e[0]=ex; e[1]=ey; e[2]=ez;
		d[0]=dx; d[1]=dy; d[2]=dz;
		for(k=0; k<3; ++k)
		{
			if(0==d[k])
			{
				if(e[k]<lo[k] || e[k]>hi[k])
				{
					tmin=tmax+1;
					break;
				}
			}
			else
			{
				int t1=(lo[k]-e[k])*16384/d[k]*1,t2=(hi[k]-e[k])*16384/d[k];
				if(t1>t2){int t=t1;t1=t2;t2=t;}
				if(t1>tmin) tmin=t1;
				if(t2<tmax) tmax=t2;
			}
		}
		if(tmin<=tmax && tmin<bestT)
		{
			bestT=tmin;
			best=i;
		}
	}
	*dist=bestT;
	return best;
}

int mobs_hostiles_near(int x,int y,int z,int radiusBlocks)
{
	int i;
	for(i=0; i<MAX_MOBS; ++i)
	{
		const Mob *m=&g_mobs[i];
		if(is_hostile(m->type))
		{
			int dx=(m->body.x-x)>>12,dy=(m->body.y-y)>>12,dz=(m->body.z-z)>>12;
			if(dx*dx+dy*dy+dz*dz<=radiusBlocks*radiusBlocks)
			{
				return 1;
			}
		}
	}
	return 0;
}

static int find_ground(int x,int z,int *y)
{
	int sy=world_surface_y(x,z);
	u8 below=wget(x,sy-1,z);
	if(!in_world(x,sy,z) || sy+2>=WH)
	{
		return 0;
	}
	if(!(blk_flags(below)&BF_OPAQUE) || B_AIR!=wget(x,sy,z) || B_AIR!=wget(x,sy+1,z))
	{
		return 0;
	}
	*y=sy;
	return 1;
}

void mobs_spawn_initial(void)
{
	int n=g_W*g_W/500,tries=n*20;
	while(n>0 && tries-->0)
	{
		int x=4+rnd_range(g_W-8),z=4+rnd_range(g_W-8),y;
		if(find_ground(x,z,&y) && B_GRASS==BLK_ID(wget(x,y-1,z)))
		{
			mob_spawn(rnd_range(2) ? MOB_PIG : MOB_SHEEP,x*FU+2048,y*FU,z*FU+2048);
			--n;
		}
	}
}

void mobs_try_spawn(int skyDarken)
{
	int ang=rnd_range(1024),r=14+rnd_range(14);
	int x=(g_player.body.x>>12)+((fsin(ang)*r)>>14),z=(g_player.body.z>>12)+((fcos(ang)*r)>>14),y;
	int maxHostile=(g_ramMB>=4 ? 8 : 5);
	if(!find_ground(x,z,&y))
	{
		return;
	}
	{
		int L=world_light_at(x,y,z);
		int eff=MAX((L>>4)-skyDarken,L&15);
		if(eff<7 && mobs_count(1)<maxHostile)
		{
			mob_spawn(rnd_range(10)<7 ? MOB_ZOMBIE : MOB_CREEPER,x*FU+2048,y*FU,z*FU+2048);
		}
		else if(eff>=9 && skyDarken<4 && mobs_count(0)<g_W*g_W/1000 && B_GRASS==BLK_ID(wget(x,y-1,z)))
		{
			mob_spawn(rnd_range(2) ? MOB_PIG : MOB_SHEEP,x*FU+2048,y*FU,z*FU+2048);
		}
	}
}
