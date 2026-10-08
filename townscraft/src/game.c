/* Game loop, player interaction and menus. */
#include "common.h"
#include "sys.h"
#include "gfx.h"
#include "video.h"
#include "textures.h"
#include "world.h"
#include "render.h"
#include "raster.h"
#include "fmath.h"
#include "player.h"
#include "mobs.h"
#include "inventory.h"
#include "ui.h"
#include "game.h"
#include "sound.h"
#include "save.h"
#include "bench.h"

int g_time;
int g_skyDarken;

enum
{
	GS_TITLE,GS_PLAY,GS_INVENTORY,GS_CRAFT,GS_HELP,GS_DEAD,GS_SLEEP,GS_CHEST
};
static int state=GS_TITLE;

/* Messages */
static char msgText[48];
static u32 msgUntil;
static char itemNameText[32];
static u32 itemNameUntil;

/* Interaction */
static int targetValid,tX,tY,tZ,tNX,tNY,tNZ,targetDist;
static int mineX,mineY,mineZ,mineProgress;
static int useRepeat,attackCooldown,mineSoundTimer;

/* Menus */
static int craftStation,craftSel,craftScroll;
static int invCursor;
static Slot invHeld;
static Chest *openChest;
static u32 sleepStart;

/* Settings */
static int showDebug;
static u32 cpuSpeedIndex;   /* Loop iterations in 100ms, measured at start */

/* View distance presets (PF3): textured distance, total distance (faces in
   between are drawn flat) */
static const u8 viewPreset[3][2]={{8,8},{8,10},{10,12}};
static int viewPresetSel;

static void apply_view_preset(int i)
{
	viewPresetSel=i;
	g_viewDist=viewPreset[i][1];
	g_flatDist=(viewPreset[i][0]<viewPreset[i][1]) ? viewPreset[i][0] : 0;
}
static u32 fpsFrames,fpsT0,fps10;
#ifdef BENCH
/* Benchmark build: after the world is generated the player stands still and
   looks in 8 fixed directions for 3 seconds each.  g_benchFrames[i] counts
   the frames drawn in direction i (read it from the emulator). */
u32 g_benchFrames[8];
static u32 benchStart;
#endif

void game_message(const char *msg)
{
	int n=MIN(strlen(msg),(int)sizeof(msgText)-1);
	memcpy(msgText,msg,n);
	msgText[n]=0;
	msgUntil=g_ticks+250;
}

static void show_item_name(void)
{
	const Slot *s=&g_inv[g_player.selected];
	if(s->item)
	{
		int n=MIN(strlen(g_itemDef[s->item].name),(int)sizeof(itemNameText)-1);
		memcpy(itemNameText,g_itemDef[s->item].name,n);
		itemNameText[n]=0;
		itemNameUntil=g_ticks+150;
	}
}

int game_sky_darken(int t)
{
	if(t<11000) return 0;
	if(t<13500) return (t-11000)*11/2500;
	if(t<22500) return 11;
	return (24000-t)*11/1500;
}

static int is_night(void)
{
	return g_time>=12500 && g_time<23300;
}

/* ---------------- Ray cast ---------------- */

/* Voxel traversal from the eye along the view direction.  Fine units. */
static void raycast(void)
{
	int ex,ey,ez,dx,dy,dz,x,y,z,stepX,stepY,stepZ;
	int tMaxX,tMaxY,tMaxZ,tDX,tDY,tDZ,t=0,face=-1;
	const int maxDist=5*FU;
	player_eye(&ex,&ey,&ez);
	player_look_dir(&dx,&dy,&dz);
	x=ex>>12; y=ey>>12; z=ez>>12;
	stepX=dx>0 ? 1 : -1;
	stepY=dy>0 ? 1 : -1;
	stepZ=dz>0 ? 1 : -1;
#define TMAX(e,d,c,step) ((d)==0 ? 0x7FFFFFFF : (int)(((long)((step)>0 ? ((c)+1)*FU-(e) : (e)-(c)*FU))*16384/ABS(d)))
	tMaxX=TMAX(ex,dx,x,stepX);
	tMaxY=TMAX(ey,dy,y,stepY);
	tMaxZ=TMAX(ez,dz,z,stepZ);
	tDX=dx ? FU*16384/ABS(dx) : 0x7FFFFFFF;
	tDY=dy ? FU*16384/ABS(dy) : 0x7FFFFFFF;
	tDZ=dz ? FU*16384/ABS(dz) : 0x7FFFFFFF;
	targetValid=0;
	while(t<=maxDist)
	{
		u8 b=wget(x,y,z);
		if(face>=0 && B_AIR!=b && B_WATER!=BLK_ID(b) && in_world(x,y,z))
		{
			targetValid=1;
			tX=x; tY=y; tZ=z;
			tNX=tNY=tNZ=0;
			switch(face)
			{
			case 0: tNX=-stepX; break;
			case 1: tNY=-stepY; break;
			default:tNZ=-stepZ; break;
			}
			targetDist=t;
			return;
		}
		if(tMaxX<tMaxY && tMaxX<tMaxZ)
		{
			x+=stepX; t=tMaxX; tMaxX+=tDX; face=0;
		}
		else if(tMaxY<tMaxZ)
		{
			y+=stepY; t=tMaxY; tMaxY+=tDY; face=1;
		}
		else
		{
			z+=stepZ; t=tMaxZ; tMaxZ+=tDZ; face=2;
		}
	}
	targetDist=maxDist;
}

/* ---------------- Breaking ---------------- */

static int held_item(void)
{
	return g_inv[g_player.selected].item;
}

static void remove_unsupported_above(int x,int y,int z)
{
	u8 b=wget(x,y+1,z);
	int id=BLK_ID(b);
	if(B_TORCH==id && 0==BLK_META(b))
	{
		world_set(x,y+1,z,B_AIR);
		inv_add(B_TORCH,1);
	}
	else if(B_FLOWER==id || B_TALLGRASS==id)
	{
		world_set(x,y+1,z,B_AIR);
		if(B_FLOWER==id)
		{
			inv_add(B_FLOWER,1);
		}
	}
	else if(B_DOOR_LOWER==id)
	{
		world_set(x,y+1,z,B_AIR);
		world_set(x,y+2,z,B_AIR);
		inv_add(I_DOOR,1);
	}
}

static void remove_wall_torches(int x,int y,int z)
{
	static const s8 off[4][3]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
	int k;
	/* A wall torch with meta m hangs on the wall at -X,+X,-Z,+Z of its cell */
	for(k=0; k<4; ++k)
	{
		u8 b=wget(x+off[k][0],y,z+off[k][2]);
		if(B_TORCH==BLK_ID(b) && BLK_META(b)==k+1)
		{
			world_set(x+off[k][0],y,z+off[k][2],B_AIR);
			inv_add(B_TORCH,1);
		}
	}
}

/* The sound of a block: pickaxe blocks sound like stone, axe blocks like
   wood, the rest like dirt */
static void block_sfx(u8 b,int x,int y,int z,int vol,int pitch)
{
	int tool=g_blockDef[BLK_ID(b)].tool;
	int id=(TOOL_PICK==tool) ? SFX_STONE : (TOOL_AXE==tool ? SFX_WOOD : SFX_CRUNCH);
	if(B_GLASS==BLK_ID(b))
	{
		pitch=pitch*3/2;
	}
	sound_play_at(id,pitch,vol,x*FU+FU/2,y*FU+FU/2,z*FU+FU/2);
}

static void break_block(int x,int y,int z)
{
	u8 b=wget(x,y,z);
	int id=BLK_ID(b);
	const BlockDef *def=&g_blockDef[id];
	int item=held_item(),tool=g_itemDef[item].tool,tier=g_itemDef[item].tier;
	int drop=def->drop;
	if(def->needTier && (tool!=def->tool || tier<def->needTier))
	{
		drop=0;
	}
	switch(id)
	{
	case B_DOOR_LOWER:
		world_set(x,y+1,z,B_AIR);
		break;
	case B_DOOR_UPPER:
		world_set(x,y-1,z,B_AIR);
		break;
	case B_BED_FOOT:
	case B_BED_HEAD:
		{
			static const s8 fdir[4][2]={{0,1},{-1,0},{0,-1},{1,0}};
			int m=BLK_META(b)&3,s=(B_BED_FOOT==id) ? 1 : -1;
			world_set(x+fdir[m][0]*s,y,z+fdir[m][1]*s,B_AIR);
		}
		break;
	case B_LEAVES:
		if(0==rnd_range(12))
		{
			inv_add(I_APPLE,1);
			game_message("+ Apple");
		}
		else if(0==rnd_range(4))
		{
			inv_add(I_STICK,1);
		}
		break;
	}
	block_sfx(b,x,y,z,255,256);
	if(B_CHEST==id && chest_remove(x,y,z,1))
	{
		game_message("Inventory full: some items were lost");
	}
	world_set(x,y,z,B_AIR);
	remove_unsupported_above(x,y,z);
	remove_wall_torches(x,y,z);
	if(drop)
	{
		if(inv_add(drop,1))
		{
			game_message("Inventory full");
		}
	}
}

static int mine_speed(int blockId)
{
	const BlockDef *def=&g_blockDef[blockId];
	int item=held_item();
	if(g_itemDef[item].tool==def->tool && TOOL_NONE!=def->tool)
	{
		static const u8 mult[5]={1,2,4,6,9};
		return mult[MIN(4,g_itemDef[item].tier)];
	}
	return 1;
}

/* ---------------- Using and placing ---------------- */

static int facing_index(void)
{
	/* 0 +Z, 1 -X, 2 -Z, 3 +X (player yaw 0 looks along +Z) */
	int q=((g_player.yaw+128)&1023)>>8;
	static const u8 map[4]={0,3,2,1};
	return map[q];
}

static int cell_blocked_by_entity(int x,int y,int z)
{
	int i;
	if(body_touches_block(&g_player.body,x,y,z))
	{
		return 1;
	}
	for(i=0; i<MAX_MOBS; ++i)
	{
		if(MOB_NONE!=g_mobs[i].type && body_touches_block(&g_mobs[i].body,x,y,z))
		{
			return 1;
		}
	}
	return 0;
}

static int replaceable(int x,int y,int z)
{
	return in_world(x,y,z) && (blk_flags(wget(x,y,z))&BF_REPLACE);
}

static void consume_held(void)
{
	inv_take_from_slot(g_player.selected,1);
}

static void try_sleep(int x,int y,int z)
{
	g_player.spawnX=x*FU+2048;
	g_player.spawnY=(y+1)*FU;
	g_player.spawnZ=z*FU+2048;
	g_player.hasBedSpawn=1;
	if(!is_night())
	{
		game_message("Respawn point set. Sleep at night.");
		return;
	}
	if(mobs_hostiles_near(g_player.body.x,g_player.body.y,g_player.body.z,8))
	{
		game_message("You may not rest, monsters nearby");
		return;
	}
	state=GS_SLEEP;
	sleepStart=g_ticks;
}

static int use_block(void)
{
	u8 b=wget(tX,tY,tZ);
	int id=BLK_ID(b);
	switch(id)
	{
	case B_CRAFT_TABLE:
		state=GS_CRAFT;
		craftStation=ST_TABLE;
		craftSel=craftScroll=0;
		return 1;
	case B_FURNACE:
		state=GS_CRAFT;
		craftStation=ST_FURNACE;
		craftSel=craftScroll=0;
		return 1;
	case B_DOOR_LOWER:
	case B_DOOR_UPPER:
		{
			int ly=(B_DOOR_LOWER==id) ? tY : tY-1;
			u8 lo=wget(tX,ly,tZ),hi=wget(tX,ly+1,tZ);
			int meta=BLK_META(lo)^4;
			world_set(tX,ly,tZ,MKBLK(BLK_ID(lo),meta));
			sound_play_at(SFX_WOOD,(meta&4) ? 200 : 170,230,tX*FU+FU/2,ly*FU+FU,tZ*FU+FU/2);
			if(B_DOOR_UPPER==BLK_ID(hi))
			{
				world_set(tX,ly+1,tZ,MKBLK(B_DOOR_UPPER,meta));
			}
		}
		return 1;
	case B_BED_FOOT:
	case B_BED_HEAD:
		try_sleep(tX,tY,tZ);
		return 1;
	case B_CHEST:
		openChest=chest_at(tX,tY,tZ,1);
		if(!openChest)
		{
			game_message("Too many chests");
			return 1;
		}
		sound_play_at(SFX_WOOD,300,200,tX*FU+FU/2,tY*FU+FU/2,tZ*FU+FU/2);
		invCursor=0;
		state=GS_CHEST;
		return 1;
	case B_TNT:
		world_set(tX,tY,tZ,B_AIR);
		mob_spawn(MOB_TNT,tX*FU+2048,tY*FU,tZ*FU+2048);
		game_message("TNT primed! Run!");
		return 1;
	}
	return 0;
}

static void place_block(void)
{
	int item=held_item(),px=tX+tNX,py=tY+tNY,pz=tZ+tNZ,block;
	if(!item)
	{
		return;
	}
	if(I_DOOR==item || I_BED==item)
	{
		int f=facing_index();
		static const s8 fdir[4][2]={{0,1},{-1,0},{0,-1},{1,0}};
		if(tNY!=1 || !(blk_flags(wget(tX,tY,tZ))&BF_OPAQUE))
		{
			return;
		}
		if(I_DOOR==item)
		{
			/* The door sits on the side of the cell nearest the player */
			static const u8 sideFor[4]={2,1,3,0};
			int side=sideFor[f];
			if(!replaceable(px,py,pz) || !replaceable(px,py+1,pz) || cell_blocked_by_entity(px,py,pz) || cell_blocked_by_entity(px,py+1,pz))
			{
				return;
			}
			world_set(px,py,pz,MKBLK(B_DOOR_LOWER,side));
			world_set(px,py+1,pz,MKBLK(B_DOOR_UPPER,side));
			block_sfx(B_PLANKS,px,py,pz,220,200);
		}
		else
		{
			int hx=px+fdir[f][0],hz=pz+fdir[f][1];
			if(!replaceable(px,py,pz) || !replaceable(hx,py,hz) || !(blk_flags(wget(hx,py-1,hz))&BF_SOLID) ||
			   cell_blocked_by_entity(px,py,pz) || cell_blocked_by_entity(hx,py,hz))
			{
				game_message("Not enough room for the bed");
				return;
			}
			world_set(px,py,pz,MKBLK(B_BED_FOOT,f));
			world_set(hx,py,hz,MKBLK(B_BED_HEAD,f));
			block_sfx(B_WOOL,px,py,pz,220,200);
		}
		consume_held();
		return;
	}
	block=g_itemDef[item].placeBlock;
	if(!block || !replaceable(px,py,pz))
	{
		return;
	}
	if(B_TORCH==block)
	{
		u8 support=wget(tX,tY,tZ);
		int meta=0;
		if(!(blk_flags(support)&BF_OPAQUE) || -1==tNY)
		{
			return;
		}
		if(tNX==1) meta=1;
		else if(tNX==-1) meta=2;
		else if(tNZ==1) meta=3;
		else if(tNZ==-1) meta=4;
		world_set(px,py,pz,MKBLK(B_TORCH,meta));
		block_sfx(B_PLANKS,px,py,pz,160,300);
		consume_held();
		return;
	}
	if((B_FLOWER==block) && B_GRASS!=BLK_ID(wget(px,py-1,pz)) && B_DIRT!=BLK_ID(wget(px,py-1,pz)))
	{
		return;
	}
	if((g_blockDef[block].flags&BF_SOLID) && cell_blocked_by_entity(px,py,pz))
	{
		return;
	}
	world_set(px,py,pz,(u8)block);
	block_sfx((u8)block,px,py,pz,220,200);
	consume_held();
}

static void try_eat(void)
{
	int item=held_item();
	if(item && g_itemDef[item].food)
	{
		if(g_player.health>=MAX_HEALTH)
		{
			game_message("You are not hungry");
			return;
		}
		g_player.health=MIN(MAX_HEALTH,g_player.health+g_itemDef[item].food);
		consume_held();
		sound_play(SFX_CRUNCH,420,220,0);
		game_message("Yum!");
	}
}

static void do_use(void)
{
	if(targetValid && (blk_flags(wget(tX,tY,tZ))&BF_USABLE))
	{
		if(use_block())
		{
			g_player.swing=6;
			return;
		}
	}
	if(g_itemDef[held_item()].food)
	{
		try_eat();
		return;
	}
	if(targetValid)
	{
		place_block();
		g_player.swing=6;
	}
}

static void do_attack(void)
{
	int ex,ey,ez,dx,dy,dz,dist,i;
	player_eye(&ex,&ey,&ez);
	player_look_dir(&dx,&dy,&dz);
	i=mobs_ray_hit(ex,ey,ez,dx,dy,dz,targetValid ? targetDist : 4*FU,&dist);
	g_player.swing=6;
	if(i>=0 && dist<=4*FU)
	{
		int dmg=MAX(1,g_itemDef[held_item()].damage);
		mob_hurt(i,dmg,g_player.body.x,g_player.body.z,1);
		attackCooldown=10;
	}
}

#ifdef TEST_SCENE
/* Debug build only: a house with a door, bed, torch and crafting table in
   front of the player, one of each mob, and a stocked inventory. */
static void test_scene(void)
{
	int bx=g_spawnX,bz=g_spawnZ+4,x,y,z,gy;
	gy=world_surface_y(bx+2,bz+2);
	for(z=-8; z<9; ++z)
	{
		for(x=-5; x<10; ++x)
		{
			for(y=0; y<8; ++y)
			{
				world_set(bx+x,gy+y,bz+z,B_AIR);
			}
			world_set(bx+x,gy-1,bz+z,B_GRASS);
		}
	}
	for(z=0; z<6; ++z)
	{
		for(x=0; x<6; ++x)
		{
			for(y=0; y<4; ++y)
			{
				int wall=(0==x || 5==x || 0==z || 5==z);
				if(3==y)
				{
					world_set(bx+x,gy+y,bz+z,B_PLANKS);
				}
				else if(wall)
				{
					world_set(bx+x,gy+y,bz+z,(1==y && (2==x || 3==x) && 5!=z) ? B_GLASS : (0==x||5==x ? B_LOG : B_PLANKS));
				}
			}
		}
	}
	world_set(bx+2,gy,bz,MKBLK(B_DOOR_LOWER,2|TEST_DOOR_OPEN));
	world_set(bx+2,gy+1,bz,MKBLK(B_DOOR_UPPER,2|TEST_DOOR_OPEN));
	world_set(bx+3,gy+1,bz,B_GLASS);
	world_set(bx+1,gy,bz+3,MKBLK(B_BED_FOOT,0));
	world_set(bx+1,gy,bz+4,MKBLK(B_BED_HEAD,0));
	world_set(bx+4,gy,bz+4,B_CRAFT_TABLE);
	world_set(bx+4,gy,bz+2,B_FURNACE);
	world_set(bx+4,gy+2,bz+1,MKBLK(B_TORCH,2));
	world_set(bx+1,gy+1,bz+1,MKBLK(B_TORCH,0));
	world_set(bx-2,gy,bz-1,MKBLK(B_TORCH,0));
	world_set(bx+6,gy,bz+1,B_TNT);
	world_set(bx+2,gy,bz-4,B_CHEST);
	mob_spawn(MOB_PIG,(bx-2)*FU,gy*FU,(bz-2)*FU);
	mob_spawn(MOB_SHEEP,(bx+6)*FU,gy*FU,(bz-1)*FU);
#ifndef TEST_NOHOSTILE
	mob_spawn(MOB_ZOMBIE,(bx-1)*FU,gy*FU,(bz+1)*FU);
	mob_spawn(MOB_CREEPER,(bx+7)*FU,gy*FU,(bz+3)*FU);
#endif
	g_player.body.x=(bx+2)*FU+2048;
#ifdef TEST_NEAR
	g_player.body.z=(bz-2)*FU+2048;
	g_player.pitch=-150;
#else
	g_player.body.z=(bz-6)*FU;
	g_player.pitch=-40;
#endif
	g_player.body.y=gy*FU;
	g_player.yaw=0;
	inv_add(B_PLANKS,64);
	inv_add(B_LOG,16);
	inv_add(I_COAL,8);
	inv_add(I_STONE_PICK,1);
	inv_add(I_WOOD_SWORD,1);
	inv_add(I_DOOR,2);
	inv_add(I_BED,1);
	inv_add(B_WOOL,3);
	inv_add(B_COBBLE,20);
	inv_add(I_RAW_PORK,3);
	g_time=TEST_TIME;
	showDebug=1;
	g_skyDarken=game_sky_darken(g_time);
}
#endif

/* ---------------- Game ticks ---------------- */

static const char *progressText="Generating world...";
static const char *titleMsg;       /* Error shown on the title screen */

static void gen_progress(int pct)
{
	gfx_wait_flip();
	gfx_clear(g_fb,C_BLACK);
	gfx_text_center(g_fb,100,progressText,C_WHITE,C_BLACK);
	gfx_frame(g_fb,80,120,160,10,C_GRAY);
	gfx_rect(g_fb,82,122,156*pct/100,6,P(R_GRASS,12));
	gfx_present();
}

/* Chunk meshes within the view distance plus a margin, so chunks are
   ready before they come into view */
#define STREAM_MARGIN 8
static void stream_world(int maxBuild)
{
	world_stream(g_player.body.x/FU,g_player.body.z/FU,g_viewDist+STREAM_MARGIN,maxBuild);
}

/* Music and messages when a world starts (generated or loaded) */
static void start_play(void)
{
	music_stop();
	music_set_gap(12000);    /* In game: 2-4 minutes of silence between plays */
	music_schedule(1500);
	ui_hud_invalidate();
}

static void load_game(void)
{
	int err;
	progressText="Loading world...";
	g_genProgress=gen_progress;
	gen_progress(0);
	player_init();
	err=load_world();
	if(SAVE_OK!=err)
	{
		titleMsg=save_error_text(err);
		progressText="Generating world...";
		return;
	}
	world_rebuild_after_load();
	progressText="Generating world...";
	mobs_clear();
	mobs_spawn_initial();
	stream_world(100000);
	start_play();
	titleMsg=NULL;
	state=GS_PLAY;
	game_message("World loaded");
}

static void save_game(void)
{
	int err;
	progressText="Saving world...";
	g_genProgress=gen_progress;
	gen_progress(0);
	err=save_world();
	progressText="Generating world...";
	ui_hud_invalidate();
	game_message(SAVE_OK==err ? "World saved" : save_error_text(err));
}

static void new_game(u32 seed)
{
	music_stop();
	music_set_gap(12000);    /* In game: 2-4 minutes of silence between plays */
	music_schedule(1500);
	g_genProgress=gen_progress;
	gen_progress(0);
	{
		extern int g_profEnable;
		g_profEnable=1;
		BENCH_HOOK(bench_gen_begin());
		world_generate(seed);
		g_profEnable=0;
	}
	inv_clear();
	chests_clear();
	mobs_clear();
	player_init();
	mobs_spawn_initial();
	g_time=1000;
	g_player.selected=0;
	/* A few starting items so the first night is survivable */
	inv_add(B_TORCH,4);
	game_message("PF1 = Help");
#ifdef TEST_SCENE
	test_scene();
#endif
#ifdef TEST_CAVE
	{
		/* Debug: stand in the nearest cave, with a torch */
		int r,x,z,y,done=0;
		for(r=0; r<g_W/2 && !done; ++r)
		{
			for(z=g_spawnZ-r; z<=g_spawnZ+r && !done; ++z)
			{
				for(x=g_spawnX-r; x<=g_spawnX+r && !done; ++x)
				{
					for(y=3; y<13 && !done; ++y)
					{
						if(B_AIR==wget(x,y,z) && B_AIR==wget(x,y+1,z) && world_is_solid(x,y-1,z) &&
						   B_AIR==wget(x,y,z+1) && B_AIR==wget(x,y+1,z+1) && world_is_solid(x,y-1,z+1))
						{
							g_player.body.x=x*FU+FU/2;
							g_player.body.y=y*FU;
							g_player.body.z=z*FU+FU/2;
							world_set(x,y,z+1,MKBLK(B_TORCH,0));
							g_player.pitch=-60;
							done=1;
						}
					}
				}
			}
		}
		g_time=6000;
	}
#endif
	stream_world(100000);
	BENCH_HOOK(bench_gen_end());
}

/* ---------------- Weather ---------------- */

static int raining,rainLevel;       /* rainLevel fades 0..16 */
static int weatherTimer=20*240;     /* Ticks to the next change */
static u32 cloudTime;               /* Ticks, for cloud drift */

static void weather_tick(void)
{
	++cloudTime;
#ifdef TEST_RAIN
	if(!raining)
	{
		raining=1;
		rainLevel=16;
		weatherTimer=20*600;
	}
#endif
	if(--weatherTimer<=0)
	{
		raining=!raining;
		/* Rain for 1-4 minutes, then clear for 3-9 */
		weatherTimer=raining ? 20*(60+rnd_range(180)) : 20*(180+rnd_range(360));
	}
	if(0==(cloudTime&3))
	{
		if(raining && rainLevel<16) ++rainLevel;
		if(!raining && rainLevel>0) --rainLevel;
	}
}

/* Is anything above the player's head (a roof keeps the rain off)? */
static int player_covered(void)
{
	int ex,ey,ez,y;
	player_eye(&ex,&ey,&ez);
	for(y=(ey>>12)+1; y<WH; ++y)
	{
		if(blk_flags(wget(ex>>12,y,ez>>12))&BF_OPAQUE)
		{
			return 1;
		}
	}
	return 0;
}

/* Rain streaks over the 3D view, and the rain sound */
static void draw_rain(void)
{
	static u32 seed=1;
	int i,n,covered;
	if(rainLevel<=0 || GS_PLAY!=state)
	{
		sound_loop(SFX_RAIN,0);
		return;
	}
	covered=player_covered() || g_player.body.headInWater;
	sound_loop(SFX_RAIN,rainLevel*(covered ? 6 : 14));
	if(covered)
	{
		return;
	}
	n=rainLevel*4;
	for(i=0; i<n; ++i)
	{
		int x,y,len,k;
		u8 *p;
		seed=seed*1103515245u+12345u;
		x=(seed>>8)%(SCR_W-4);
		y=(seed>>17)%(VIEW_H-12);
		len=6+(seed&3);
		p=g_fb+y*FB_PITCH+x;
		for(k=0; k<len; ++k)
		{
			p[k*FB_PITCH+k/4]=P(R_SKY,8);
		}
	}
}

static void game_tick(const PlayerInput *in,int breakHeld)
{
	g_time=(g_time+2)%24000;
	weather_tick();
	/* Rain dims the daylight */
	g_skyDarken=MIN(11,game_sky_darken(g_time)+rainLevel*3/16);
	player_tick(in);
	{
		/* Footsteps about every 1.6 blocks walked on the ground */
		static int lastX,lastZ,stepDist;
		const Body *b=&g_player.body;
		int d=ABS(b->x-lastX)+ABS(b->z-lastZ);
		lastX=b->x;
		lastZ=b->z;
		if(b->onGround && !b->inWater && d<FU)
		{
			stepDist+=d;
			if(stepDist>FU*8/5)
			{
				int bx=b->x>>12,by=(b->y>>12)-1,bz=b->z>>12;
				int tool=g_blockDef[BLK_ID(wget(bx,by,bz))].tool;
				stepDist=0;
				sound_play(SFX_STEP,(TOOL_PICK==tool) ? 300 : (TOOL_AXE==tool ? 250 : 210),200,0);
			}
		}
	}
	mobs_tick(g_skyDarken);
	{
		static int spawnTimer;
		if(++spawnTimer>=15)
		{
			spawnTimer=0;
			mobs_try_spawn(g_skyDarken);
		}
	}
	if(attackCooldown>0)
	{
		--attackCooldown;
	}
	/* Mining */
	if(breakHeld && targetValid && !g_player.dead)
	{
		u8 b=wget(tX,tY,tZ);
		int hard=g_blockDef[BLK_ID(b)].hardness;
		if(tX!=mineX || tY!=mineY || tZ!=mineZ)
		{
			mineX=tX; mineY=tY; mineZ=tZ;
			mineProgress=0;
		}
		if(hard<255)
		{
			mineProgress+=mine_speed(BLK_ID(b));
			g_player.swing=4;
			if(0==(++mineSoundTimer&3))
			{
				block_sfx(b,tX,tY,tZ,110,330);   /* Hitting */
			}
			if(mineProgress>=hard)
			{
				break_block(tX,tY,tZ);
				mineProgress=0;
				mineX=-1;
			}
		}
	}
	else
	{
		mineProgress=0;
		mineX=-1;
	}
	if(useRepeat>0)
	{
		--useRepeat;
	}
	if(g_player.dead)
	{
		state=GS_DEAD;
	}
}

/* ---------------- Drawing ---------------- */

static void draw_world(void)
{
	RenderEnv env;
	int ex,ey,ez,level;
	memset(&env,0,sizeof(env));
	player_eye(&ex,&ey,&ez);
	g_cam.x=FU_TO_RU(ex);
	g_cam.y=FU_TO_RU(ey);
	g_cam.z=FU_TO_RU(ez);
	g_cam.yaw=g_player.yaw;
	g_cam.pitch=g_player.pitch;
	sound_set_listener(ex,ey,ez,g_player.yaw);
	env.skyDarken=g_skyDarken;
	env.sunAngle=g_time*1024/24000;
	level=15-g_skyDarken;
	if(g_skyDarken>=8)
	{
		env.skyColor=P(R_BLUE,1+(11-g_skyDarken));
		env.fogColor=P(R_BLUE,3+(11-g_skyDarken));
	}
	else
	{
		env.skyColor=P(R_SKY,MAX(2,level-1));
		env.fogColor=P(R_SKY,MIN(15,level));
		/* Sunrise and sunset glow at the horizon */
		if((g_time>11000 && g_time<13500) || g_time>22300)
		{
			env.fogColor=P(R_FLAME,MAX(4,level-2));
		}
	}
	if(rainLevel>=8 && g_skyDarken<8)
	{
		/* Overcast */
		env.skyColor=P(R_GRAY,MAX(4,level-3));
		env.fogColor=P(R_GRAY,MAX(5,level-2));
	}
	env.starBrightness=(g_skyDarken>=6 && rainLevel<8) ? g_skyDarken+2 : 0;
	env.cloudDrift=(int)(cloudTime*8);
	env.cloudColor=P(R_GRAY,MAX(3,14-g_skyDarken-rainLevel/4));
	env.targetValid=targetValid;
	env.tx=tX; env.ty=tY; env.tz=tZ;
	env.underwater=g_player.body.headInWater;
	{
		/* Underground: eye below sea level, under the column's top and
		   with little sky light (a shaft open to the sky still shows it) */
		int bx=ex/FU,by=ey/FU,bz=ez/FU;
		if(in_world(bx,by,bz) && by<SEA_LEVEL && by<g_height[bz*g_W+bx]-1 &&
		   (world_light_at(bx,by,bz)>>4)<=4)
		{
			env.underground=1;
			env.skyColor=P(R_GRAY,1);
			env.fogColor=env.skyColor;
		}
	}
	if(env.underwater)
	{
		env.skyColor=P(R_WATER,MAX(2,8-g_skyDarken/2));
		env.fogColor=env.skyColor;
	}
	render_clear_boxes();
	mobs_add_render_boxes(g_skyDarken);
	render_frame(g_fb,&env);
}

static void draw_hud(void)
{
	char buf[64];
	ui_crosshair(g_fb);
	ui_hud_strip(g_fb,gfx_back_page(),g_player.selected,g_player.health,g_player.hurtTimer>0 || g_player.health<=4,
	             g_player.airTimer>0 ? MAX(0,10-g_player.airTimer/20) : -1);
	if(mineProgress>0 && mineX>=0)
	{
		int hard=g_blockDef[BLK_ID(wget(mineX,mineY,mineZ))].hardness;
		int w=hard ? mineProgress*30/hard : 0;
		gfx_rect(g_fb,SCR_W/2-15,VIEW_H/2+10,30,3,P(R_GRAY,3));
		gfx_rect(g_fb,SCR_W/2-15,VIEW_H/2+10,MIN(30,w),3,C_WHITE);
	}
	if(g_ticks<itemNameUntil)
	{
		gfx_text_center(g_fb,VIEW_H-12,itemNameText,C_WHITE,C_BLACK);
	}
	if(g_ticks<msgUntil)
	{
		gfx_text_center(g_fb,VIEW_H-24,msgText,C_YELLOW,C_BLACK);
	}
	if(showDebug)
	{
		int y=2;
		itoa_dec(fps10/10,buf);
		{
			char *p=buf+strlen(buf);
			*p++='.';
			*p++='0'+fps10%10;
			memcpy(p," fps",5);
		}
		gfx_text_shadow(g_fb,2,y,buf,C_WHITE,C_BLACK); y+=10;
		gfx_text_shadow(g_fb,2,y,"XYZ",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,34,y,itoa_dec(g_player.body.x>>12,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,74,y,itoa_dec(g_player.body.y>>12,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,106,y,itoa_dec(g_player.body.z>>12,buf),C_WHITE,C_BLACK); y+=10;
		gfx_text_shadow(g_fb,2,y,"Faces",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,50,y,itoa_dec(g_statFaces,buf),C_WHITE,C_BLACK); y+=10;
		gfx_text_shadow(g_fb,2,y,"Yaw",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,34,y,itoa_dec(g_player.yaw,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,82,y,itoa_dec(g_player.pitch,buf),C_WHITE,C_BLACK); y+=10;
		gfx_text_shadow(g_fb,2,y,"Time",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,42,y,itoa_dec(g_time,buf),C_WHITE,C_BLACK); y+=10;
		gfx_text_shadow(g_fb,2,y,"View",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,42,y,itoa_dec(g_viewDist,buf),C_WHITE,C_BLACK); y+=10;
		{
			int i,n=0,best=-1,bd=1<<30;
			for(i=0; i<MAX_MOBS; ++i)
			{
				if(g_mobs[i].type)
				{
					int dx=(g_mobs[i].body.x-g_player.body.x)>>12,dz=(g_mobs[i].body.z-g_player.body.z)>>12;
					++n;
					if(dx*dx+dz*dz<bd)
					{
						bd=dx*dx+dz*dz;
						best=i;
					}
				}
			}
			gfx_text_shadow(g_fb,2,y,"Mobs",C_WHITE,C_BLACK);
			gfx_text_shadow(g_fb,42,y,itoa_dec(n,buf),C_WHITE,C_BLACK);
			if(best>=0)
			{
				gfx_text_shadow(g_fb,66,y,itoa_dec(g_mobs[best].type,buf),C_WHITE,C_BLACK);
				gfx_text_shadow(g_fb,82,y,itoa_dec((g_mobs[best].body.x-g_player.body.x)>>12,buf),C_WHITE,C_BLACK);
				gfx_text_shadow(g_fb,114,y,itoa_dec((g_mobs[best].body.y-g_player.body.y)>>12,buf),C_WHITE,C_BLACK);
				gfx_text_shadow(g_fb,146,y,itoa_dec((g_mobs[best].body.z-g_player.body.z)>>12,buf),C_WHITE,C_BLACK);
			}
			y+=10;
			gfx_text_shadow(g_fb,2,y,"Items",C_WHITE,C_BLACK);
			gfx_text_shadow(g_fb,50,y,itoa_dec(g_statItems,buf),C_WHITE,C_BLACK);
			{
				extern u32 g_dbg[4];
				int j;
				for(j=0; j<3; ++j)
				{
					gfx_text_shadow(g_fb,90+j*40,y,itoa_dec(g_dbg[j],buf),C_YELLOW,C_BLACK);
				}
				g_dbg[1]=g_dbg[2]=0;
			}
			y+=10;
		}
	}
}

static void draw_title(void)
{
	static const char *lines[]=
	{
		"A block building game for FM TOWNS",
		"",
		"SPACE  Start a new world",
		"L      Load world (floppy A)",
		"PF9    Save (in game)",
		"PF1    Help / controls",
	};
	int i;
	char buf[48];
	gfx_clear(g_fb,P(R_SKY,12));
	gfx_rect(g_fb,0,150,SCR_W,90,P(R_DIRT,9));
	gfx_rect(g_fb,0,146,SCR_W,6,P(R_GRASS,11));
	ui_text_scaled(g_fb,24+3,40+3,"TOWNSCRAFT",P(R_GRAY,3),3);
	ui_text_scaled(g_fb,24,40,"TOWNSCRAFT",C_WHITE,3);
	for(i=0; i<ARRAY_LEN(lines); ++i)
	{
		gfx_text_center(g_fb,84+i*12,lines[i],C_WHITE,C_BLACK);
	}
	memcpy(buf,"RAM ",4);
	itoa_dec(g_ramMB,buf+4);
	memcpy(buf+strlen(buf),"MB  World ",11);
	itoa_dec(g_W,buf+strlen(buf));
	memcpy(buf+strlen(buf),"x",2);
	itoa_dec(g_W,buf+strlen(buf));
	gfx_text_center(g_fb,200,buf,C_WHITE,C_BLACK);
	if(titleMsg)
	{
		gfx_text_center(g_fb,176,titleMsg,C_YELLOW,C_BLACK);
	}
}

static void draw_help(void)
{
	static const char *lines[]=
	{
		"CONTROLS",
		"W A S D     Move      CTRL  Sprint",
		"Arrows      Look      SPACE Jump/Swim",
		"J (hold)    Break / attack",
		"K           Use / place / eat",
		"1-9  , .    Select hotbar slot",
		"E           Inventory",
		"C           Craft (by hand)",
		"Use a Crafting Table or Furnace",
		"for more recipes.  Use a Bed at",
		"night to sleep.",
		"Mouse (port B): look, L/R buttons",
		"PF2 Res.  PF3 View  PF4 Debug",
		"PF6 Music  PF7 Sound  ESC Back",
	};
	int i;
	ui_panel(g_fb,20,16,280,208);
	for(i=0; i<ARRAY_LEN(lines); ++i)
	{
		gfx_text_shadow(g_fb,30,24+i*14,lines[i],i==0 ? C_YELLOW : C_WHITE,C_BLACK);
	}
}

/* ---------------- Crafting screen ---------------- */

static int craft_list(int *out,int max)
{
	int i,n=0;
	for(i=0; i<g_numRecipes && n<max; ++i)
	{
		if(recipe_available(&g_recipes[i],craftStation))
		{
			out[n++]=i;
		}
	}
	return n;
}

static void draw_craft(void)
{
	int list[64],n=craft_list(list,64),i,rows=9;
	const char *title=(ST_FURNACE==craftStation) ? "FURNACE" : (ST_TABLE==craftStation ? "CRAFTING TABLE" : "CRAFTING");
	char buf[32];
	ui_panel(g_fb,8,8,304,224);
	gfx_text_shadow(g_fb,16,14,title,C_YELLOW,C_BLACK);
	if(craftSel>=n) craftSel=n-1;
	if(craftSel<0) craftSel=0;
	if(craftSel<craftScroll) craftScroll=craftSel;
	if(craftSel>=craftScroll+rows) craftScroll=craftSel-rows+1;
	for(i=0; i<rows && craftScroll+i<n; ++i)
	{
		const Recipe *r=&g_recipes[list[craftScroll+i]];
		int y=28+i*20,ok=recipe_can_craft(r),k;
		int sel=(craftScroll+i==craftSel);
		if(sel)
		{
			gfx_rect(g_fb,14,y-1,292,20,P(R_GRAY,7));
		}
		ui_icon(g_fb,18,y+1,r->out);
		if(r->outCount>1)
		{
			ui_small_number(g_fb,35,y+12,r->outCount,C_WHITE);
		}
		gfx_text_shadow(g_fb,40,y+5,g_itemDef[r->out].name,ok ? C_WHITE : P(R_GRAY,9),C_BLACK);
		/* Inputs */
		for(k=0; k<3; ++k)
		{
			if(r->in[k][0])
			{
				int x=236+k*24-(r->in[1][0] ? 0 : -24)-(r->in[2][0] ? 0 : -24);
				int have=inv_count(r->in[k][0]);
				ui_icon(g_fb,x,y+1,r->in[k][0]);
				ui_small_number(g_fb,x+17,y+12,r->in[k][1],have>=r->in[k][1] ? C_WHITE : C_RED);
			}
		}
	}
	if(ST_FURNACE==craftStation)
	{
		memcpy(buf,"Fuel: ",7);
		itoa_dec(g_furnaceFuel,buf+6);
		gfx_text_shadow(g_fb,150,14,buf,C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,16,212,"Burns coal, logs, planks, sticks",P(R_GRAY,11),C_BLACK);
	}
	else
	{
		gfx_text_shadow(g_fb,16,212,"Up/Down select  SPACE craft  ESC",P(R_GRAY,11),C_BLACK);
	}
	if(g_ticks<msgUntil)
	{
		gfx_text_shadow(g_fb,16,200,msgText,C_YELLOW,C_BLACK);
	}
}

static void craft_key(int k)
{
	int list[64],n=craft_list(list,64);
	switch(k)
	{
	case KEY_UP: case KEY_W: case KEY_NUM_8: --craftSel; break;
	case KEY_DOWN: case KEY_S: case KEY_NUM_2: ++craftSel; break;
	case KEY_SPACE: case KEY_RETURN: case KEY_K: case KEY_NUM_RETURN:
		if(craftSel>=0 && craftSel<n)
		{
			const Recipe *r=&g_recipes[list[craftSel]];
			if(recipe_craft(r))
			{
				sound_play(SFX_CLICK,256,200,0);
				game_message("Crafted!");
			}
			else
			{
				game_message(ST_FURNACE==craftStation && recipe_available(r,ST_FURNACE) ? "Missing items or fuel" : "Missing items");
			}
		}
		break;
	case KEY_ESC: case KEY_E: case KEY_C:
		state=GS_PLAY;
		break;
	}
	craftSel=CLAMP(craftSel,0,n-1);
}

/* ---------------- Inventory screen ---------------- */

static int inv_slot_at(int cursor)
{
	/* Rows 0-2: storage 9..35, row 3: hotbar 0..8 */
	int row=cursor/9,col=cursor%9;
	return row<3 ? 9+row*9+col : col;
}

static void close_inventory(void);

/* Chest screen: rows 0-2 the chest, rows 3-6 the inventory (as on the
   inventory screen) */
static Slot *chest_slot(int cursor)
{
	return cursor<CHEST_SLOTS ? &openChest->slot[cursor] : &g_inv[inv_slot_at(cursor-CHEST_SLOTS)];
}

static void chest_key(int k)
{
	Slot *slot=chest_slot(invCursor);
	switch(k)
	{
	case KEY_LEFT: case KEY_A: case KEY_NUM_4: if(invCursor%9) --invCursor; break;
	case KEY_RIGHT: case KEY_D: case KEY_NUM_6: if(invCursor%9<8) ++invCursor; break;
	case KEY_UP: case KEY_W: case KEY_NUM_8: if(invCursor>=9) invCursor-=9; break;
	case KEY_DOWN: case KEY_S: case KEY_NUM_2: if(invCursor<54) invCursor+=9; break;
	case KEY_SPACE: case KEY_RETURN: case KEY_K: case KEY_NUM_RETURN:
		{
			Slot t=*slot;
			if(invHeld.item && t.item==invHeld.item)
			{
				int n=MIN(invHeld.count,g_itemDef[t.item].maxStack-t.count);
				slot->count+=n;
				invHeld.count-=n;
				if(0==invHeld.count)
				{
					invHeld.item=0;
				}
			}
			else
			{
				*slot=invHeld;
				invHeld=t;
			}
			sound_play(SFX_CLICK,256,120,0);
		}
		break;
	case KEY_Q:
		if(invHeld.item)
		{
			invHeld.item=invHeld.count=0;
		}
		else
		{
			slot->item=slot->count=0;
		}
		break;
	case KEY_ESC: case KEY_E:
		close_inventory();
		break;
	}
}

static void draw_chest(void)
{
	int i,x0=(SCR_W-9*20)/2,y0=24;
	const Slot *cur=chest_slot(invCursor);
	ui_panel(g_fb,x0-12,6,9*20+24,228);
	gfx_text_shadow(g_fb,x0-4,12,"CHEST",C_YELLOW,C_BLACK);
	for(i=0; i<CHEST_SLOTS+36; ++i)
	{
		int row=i/9,col=i%9;
		int y=y0+row*20+(row>=3 ? 16 : 0)+(row==6 ? 4 : 0);
		ui_slot(g_fb,x0+col*20,y,chest_slot(i),i==invCursor);
		if(i==invCursor && invHeld.item)
		{
			ui_icon(g_fb,x0+col*20+8,y+8,invHeld.item);
		}
	}
	gfx_text_shadow(g_fb,x0-4,y0+60+3,"INVENTORY",C_YELLOW,C_BLACK);
	if(invHeld.item || cur->item)
	{
		gfx_text_shadow(g_fb,x0-4,y0+7*20+22,g_itemDef[invHeld.item ? invHeld.item : cur->item].name,C_WHITE,C_BLACK);
	}
	gfx_text_shadow(g_fb,x0-4,y0+7*20+34,"SPACE move  Q discard  ESC",P(R_GRAY,11),C_BLACK);
}

static void draw_inventory(void)
{
	int i,x0=(SCR_W-9*20)/2,y0=50;
	ui_panel(g_fb,x0-12,20,9*20+24,170);
	gfx_text_shadow(g_fb,x0-4,28,"INVENTORY",C_YELLOW,C_BLACK);
	for(i=0; i<36; ++i)
	{
		int row=i/9,col=i%9;
		int y=y0+row*22+(row==3 ? 6 : 0);
		ui_slot(g_fb,x0+col*20,y,&g_inv[inv_slot_at(i)],i==invCursor);
	}
	if(invHeld.item)
	{
		int row=invCursor/9,col=invCursor%9;
		int y=y0+row*22+(row==3 ? 6 : 0);
		ui_icon(g_fb,x0+col*20+8,y+8,invHeld.item);
		gfx_text_shadow(g_fb,x0-4,y0+4*22+10,g_itemDef[invHeld.item].name,C_WHITE,C_BLACK);
	}
	else if(g_inv[inv_slot_at(invCursor)].item)
	{
		gfx_text_shadow(g_fb,x0-4,y0+4*22+10,g_itemDef[g_inv[inv_slot_at(invCursor)].item].name,C_WHITE,C_BLACK);
	}
	gfx_text_shadow(g_fb,x0-4,y0+4*22+24,"SPACE move  Q discard",P(R_GRAY,11),C_BLACK);
	ui_hotbar(g_fb,g_player.selected);
}

static void close_inventory(void)
{
	if(invHeld.item)
	{
		inv_add(invHeld.item,invHeld.count);
		invHeld.item=invHeld.count=0;
	}
	state=GS_PLAY;
}

static void inventory_key(int k)
{
	int slot=inv_slot_at(invCursor);
	switch(k)
	{
	case KEY_LEFT: case KEY_A: case KEY_NUM_4: if(invCursor%9) --invCursor; break;
	case KEY_RIGHT: case KEY_D: case KEY_NUM_6: if(invCursor%9<8) ++invCursor; break;
	case KEY_UP: case KEY_W: case KEY_NUM_8: if(invCursor>=9) invCursor-=9; break;
	case KEY_DOWN: case KEY_S: case KEY_NUM_2: if(invCursor<27) invCursor+=9; break;
	case KEY_SPACE: case KEY_RETURN: case KEY_K: case KEY_NUM_RETURN:
		{
			Slot t=g_inv[slot];
			if(invHeld.item && t.item==invHeld.item)
			{
				int maxS=g_itemDef[t.item].maxStack;
				int n=MIN(invHeld.count,maxS-t.count);
				g_inv[slot].count+=n;
				invHeld.count-=n;
				if(0==invHeld.count)
				{
					invHeld.item=0;
				}
			}
			else
			{
				g_inv[slot]=invHeld;
				invHeld=t;
			}
		}
		break;
	case KEY_Q:
		if(invHeld.item)
		{
			invHeld.item=invHeld.count=0;
		}
		else
		{
			g_inv[slot].item=g_inv[slot].count=0;
		}
		break;
	case KEY_ESC: case KEY_E:
		close_inventory();
		break;
	}
}

/* ---------------- Settings ---------------- */

static void auto_detect_quality(void)
{
	/* Rough CPU speed: loop iterations in 100ms */
	volatile u32 n=0;
	u32 t=g_ticks;
	while(t==g_ticks);
	t=g_ticks;
	while(g_ticks-t<10)
	{
		++n;
	}
	cpuSpeedIndex=n;
	/* Measured in Tsugaru: 386 16MHz ~120000, default profile ~290000 */
	if(n<170000)
	{
		g_renderScale=2;
		apply_view_preset(0);
	}
	else if(n<450000)
	{
		g_renderScale=2;
		apply_view_preset(2);
	}
	else
	{
		g_renderScale=1;
		apply_view_preset(2);
	}
}

static void settings_key(int k)
{
	switch(k)
	{
	case KEY_PF2:
		g_renderScale=3-g_renderScale;
		game_message(1==g_renderScale ? "Resolution: 320x200" : "Resolution: 160x100");
		break;
	case KEY_PF6:
		g_musicOn=!g_musicOn;
		game_message(g_musicOn ? "Music on" : "Music off");
		break;
	case KEY_PF7:
		g_sfxOn=!g_sfxOn;
		game_message(g_sfxOn ? "Sound effects on" : "Sound effects off");
		break;
	case KEY_PF9:
		if(GS_PLAY==state)
		{
			save_game();
		}
		break;
	case KEY_PF8:
		g_interlace=!g_interlace;
		game_message(g_interlace ? "Interlaced rendering on" : "Interlaced rendering off");
		break;
	case KEY_PF3:
		{
			static const char *const names[3]=
			{
				"View: 8 blocks",
				"View: 8 textured, 10 flat",
				"View: 10 textured, 12 flat",
			};
			apply_view_preset((viewPresetSel+1)%3);
			game_message(names[viewPresetSel]);
		}
		break;
	case KEY_PF4:
		showDebug=!showDebug;
		break;
	case KEY_PF5:
		{
			/* Sampling profiler (dump g_profSamples from the emulator) */
			extern int g_profEnable;
			extern u32 g_profCount;
			g_profEnable=!g_profEnable;
			g_profCount=0;
			game_message(g_profEnable ? "Profiler on" : "Profiler off");
		}
		break;
	}
}

/* ---------------- Main loop ---------------- */

static void play_key(int k)
{
	switch(k)
	{
	case KEY_1: case KEY_2: case KEY_3: case KEY_4: case KEY_5:
	case KEY_6: case KEY_7: case KEY_8: case KEY_9:
		g_player.selected=k-KEY_1;
		show_item_name();
		break;
	case KEY_COMMA:
		g_player.selected=(g_player.selected+8)%9;
		show_item_name();
		break;
	case KEY_DOT:
		g_player.selected=(g_player.selected+1)%9;
		show_item_name();
		break;
	case KEY_E:
		state=GS_INVENTORY;
		invCursor=27+g_player.selected;
		break;
	case KEY_C:
		state=GS_CRAFT;
		craftStation=ST_HAND;
		craftSel=craftScroll=0;
		break;
	case KEY_K:
		do_use();
		useRepeat=5;
		break;
	case KEY_J:
		if(0==attackCooldown)
		{
			do_attack();
		}
		break;
	case KEY_ESC:
	case KEY_PF1:
		state=GS_HELP;
		break;
	}
}

void kmain(void)
{
	u32 lastTick,tickAccum=0,padPrev=0;
	int syncPages=0,mousePrev=0;
	video_init();
	gfx_init();
	sys_init();
	items_init();
	textures_init();
	palette_apply();
	world_alloc();
	render_init();
	auto_detect_quality();
	sound_init();
	music_set_gap(800);      /* Title screen: play again after 8-16 s */
	music_schedule(100);
	lastTick=g_ticks;
	fpsT0=g_ticks;
	for(;;)
	{
		u32 now=g_ticks,elapsed=now-lastTick;
		u32 pad=pad_read();
		u32 padPressed=pad&~padPrev;
		int mouseDX,mouseDY,mouse=mouse_read(&mouseDX,&mouseDY);
		int mousePressed=mouse&~mousePrev;
		int k;
		lastTick=now;
		padPrev=pad;
		mousePrev=mouse;
		if(elapsed>20)
		{
			elapsed=20;   /* After a hitch, don't fast-forward */
		}

		while((k=key_get_event())>=0)
		{
			settings_key(k);
			switch(state)
			{
			case GS_TITLE:
				if(KEY_SPACE==k || KEY_RETURN==k)
				{
#if defined(TEST_SCENE)
					new_game(12345);
#elif defined(FIXED_SEED)
					new_game(FIXED_SEED);
#else
					new_game(g_ticks*2654435761u+12345);
#endif
					state=GS_PLAY;
				}
				else if(KEY_L==k)
				{
					load_game();
				}
				else if(KEY_PF1==k)
				{
					state=GS_HELP;
				}
				break;
			case GS_PLAY: play_key(k); break;
			case GS_INVENTORY: inventory_key(k); break;
			case GS_CHEST: chest_key(k); break;
			case GS_CRAFT: craft_key(k); break;
			case GS_HELP:
				if(KEY_ESC==k || KEY_PF1==k || KEY_SPACE==k)
				{
					state=(0==g_W || 0==g_player.body.h) ? GS_TITLE : GS_PLAY;
				}
				break;
			case GS_DEAD:
				if(KEY_SPACE==k || KEY_RETURN==k)
				{
					player_respawn();
					state=GS_PLAY;
				}
				break;
			}
		}
		/* Game pad: SELECT cycles the hotbar */
		if(GS_PLAY==state && (padPressed&PAD_SELECT))
		{
			g_player.selected=(g_player.selected+1)%9;
			show_item_name();
		}
		if(GS_PLAY==state && ((padPressed&PAD_A) || (mousePressed&MOUSE_L)) && 0==attackCooldown)
		{
			do_attack();
		}
		if(GS_PLAY==state && ((padPressed&PAD_B) || (mousePressed&MOUSE_R)))
		{
			do_use();
			useRepeat=5;
		}

#ifdef BENCH_EDIT
		if(GS_TITLE==state)
		{
			new_game(FIXED_SEED);
			state=GS_PLAY;
		}
		if(GS_PLAY==state)
		{
			bench_frame();
		}
#endif
		if(GS_TITLE==state)
		{
			gfx_wait_flip();
			draw_title();
			gfx_present();
			continue;
		}

#ifdef BENCH
		if(GS_PLAY==state)
		{
			u32 phase;
			if(0==benchStart)
			{
				benchStart=g_ticks;
			}
			phase=(g_ticks-benchStart)/300;
			if(phase<8)
			{
				g_player.yaw=phase*128;
				g_player.pitch=-40;
				++g_benchFrames[phase];
			}
		}
#endif
		/* Looking around is per frame for smoothness */
		if(GS_PLAY==state && !g_player.dead)
		{
			int turn=(int)elapsed*6;
			if(g_keyDown[KEY_LEFT] || g_keyDown[KEY_NUM_4] || (pad&PAD_LEFT)) g_player.yaw-=turn;
			if(g_keyDown[KEY_RIGHT] || g_keyDown[KEY_NUM_6] || (pad&PAD_RIGHT)) g_player.yaw+=turn;
			if(g_keyDown[KEY_UP] || g_keyDown[KEY_NUM_8]) g_player.pitch+=turn*2/3;
			if(g_keyDown[KEY_DOWN] || g_keyDown[KEY_NUM_2]) g_player.pitch-=turn*2/3;
			/* Mouse: one count turns 1/1024 of a circle */
			g_player.yaw+=mouseDX;
			g_player.pitch-=mouseDY;
			g_player.yaw&=ANG_MASK;
			g_player.pitch=CLAMP(g_player.pitch,-250,250);
		}

		/* Fixed 20Hz game ticks (the timer runs at 100Hz) */
		if(GS_PLAY==state || GS_DEAD==state || GS_SLEEP==state)
		{
			PlayerInput in;
			int breakHeld=0,n=0;
			memset(&in,0,sizeof(in));
			if(GS_PLAY==state)
			{
				in.forward=(g_keyDown[KEY_W] || (pad&PAD_UP) ? 1 : 0)-(g_keyDown[KEY_S] || (pad&PAD_DOWN) ? 1 : 0);
				in.strafe=(g_keyDown[KEY_D] ? 1 : 0)-(g_keyDown[KEY_A] ? 1 : 0);
				in.jump=g_keyDown[KEY_SPACE] || (pad&PAD_RUN);
				in.sprint=g_keyDown[KEY_CTRL];
				breakHeld=g_keyDown[KEY_J] || (pad&PAD_A) || (mouse&MOUSE_L);
			}
			tickAccum+=elapsed;
			while(tickAccum>=5 && n<4)
			{
				tickAccum-=5;
				++n;
				raycast();
				game_tick(&in,breakHeld);
				if(GS_PLAY==state && (g_keyDown[KEY_K] || (pad&PAD_B) || (mouse&MOUSE_R)) && 0==useRepeat)
				{
					do_use();
					useRepeat=5;
				}
				if(GS_PLAY==state && breakHeld && 0==attackCooldown)
				{
					int ex,ey,ez,dx,dy,dz,dist;
					player_eye(&ex,&ey,&ez);
					player_look_dir(&dx,&dy,&dz);
					if(mobs_ray_hit(ex,ey,ez,dx,dy,dz,targetValid ? targetDist : 4*FU,&dist)>=0)
					{
						do_attack();
					}
				}
			}
			if(tickAccum>=5)
			{
				tickAccum=0;
			}
			raycast();
			/* Budgeted relighting of chunks whose light changed */
			BENCH_HOOK(bench_update_begin());
			world_update_dirty_chunks(1);
			/* Mesh chunks coming into range, one per frame */
			stream_world(1);
			BENCH_HOOK(bench_update_end());
		}

		{
			/* Menus pause the game.  Render the world once when a menu opens
			   and keep it as a frozen, dimmed background, so menus stay
			   responsive on slow machines. */
			static int menuFrozen;
			int inMenu=(GS_INVENTORY==state || GS_CRAFT==state || GS_HELP==state || GS_CHEST==state);
			if(inMenu && menuFrozen)
			{
				gfx_wait_flip();
			}
			if(!inMenu || !menuFrozen)
			{
				draw_world();
				draw_rain();
				if(GS_PLAY==state || inMenu)
				{
					draw_hud();
				}
				if(inMenu)
				{
					gfx_darken(g_fb,0,0,SCR_W,SCR_H);
				}
			}
			syncPages=(inMenu && !menuFrozen);
			menuFrozen=inMenu;
		}
		switch(state)
		{
		case GS_INVENTORY:
			draw_inventory();
			break;
		case GS_CHEST:
			draw_chest();
			break;
		case GS_CRAFT:
			draw_craft();
			break;
		case GS_HELP:
			draw_help();
			break;
		case GS_DEAD:
			gfx_darken(g_fb,0,0,SCR_W,SCR_H);
			ui_text_scaled(g_fb,64,80,"You died!",C_RED,3);
			gfx_text_center(g_fb,130,"Press SPACE to respawn",C_WHITE,C_BLACK);
			break;
		case GS_SLEEP:
			{
				u32 t=g_ticks-sleepStart;
				int i;
				for(i=0; i<(int)MIN(4u,t/40u); ++i)
				{
					gfx_darken(g_fb,0,0,SCR_W,SCR_H);
				}
				gfx_text_center(g_fb,116,"Sleeping...",C_WHITE,C_BLACK);
				if(t>220)
				{
					g_time=0;
					g_skyDarken=0;
					state=GS_PLAY;
					game_message("Good morning!");
				}
			}
			break;
		}
		gfx_present();
		if(syncPages)
		{
			/* The frozen background now has to be on both pages */
			gfx_sync_pages();
		}

		if(GS_PLAY!=state)
		{
			ui_hud_invalidate();   /* Menus and effects drew over the HUD strip */
		}
		++fpsFrames;
		if(g_ticks-fpsT0>=100)
		{
			fps10=fpsFrames*1000/(g_ticks-fpsT0);
			fpsFrames=0;
			fpsT0=g_ticks;
		}
	}
}
