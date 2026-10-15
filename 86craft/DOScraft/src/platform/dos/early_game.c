/* DOS early-game systems: tool wear and the save extension section. */
#include "early_game.h"
#include "player.h"
#include "game.h"
#include "sound.h"
#include "world.h"
#include "gfx.h"
#include "textures.h"

#define EXTRAS_MAGIC 0x31584745u /* "EGX1" */

/* ---------------- Campfires ---------------- */

#define MAX_FIRES 32
#define TICKS_PER_MINUTE 1200          /* 20 Hz game ticks */
#define FIRE_MAX_FUEL (20*TICKS_PER_MINUTE)  /* About one 20-minute day */

typedef struct
{
	s16 x,z;
	u8 y,used;
	u16 fuel;                      /* Game ticks left */
} Fire;

static Fire fires[MAX_FIRES];
static int openFire=-1;

static int fire_find(int x,int y,int z)
{
	int i;
	for(i=0; i<MAX_FIRES; ++i)
		if(fires[i].used && fires[i].x==x && fires[i].y==y && fires[i].z==z) return i;
	return -1;
}

static int fire_new(int x,int y,int z)
{
	int i;
	for(i=0; i<MAX_FIRES; ++i)
		if(!fires[i].used)
		{
			fires[i].x=x; fires[i].y=y; fires[i].z=z;
			fires[i].used=1; fires[i].fuel=0;
			return i;
		}
	return -1;
}

static int fuel_ticks(int item)
{
	switch(item)
	{
	case I_STICK: return 3*TICKS_PER_MINUTE/2;
	case B_PLANKS: return 3*TICKS_PER_MINUTE;
	case B_LOG: return 6*TICKS_PER_MINUTE;
	case I_COAL: case I_CHARCOAL: return 12*TICKS_PER_MINUTE;
	}
	return 0;
}

void dos_block_placed(int id,int x,int y,int z)
{
	if(B_FIRE==id)
	{
		/* Two sticks: one is the firestarter, the other burns. */
		int i=fire_find(x,y,z);
		if(i<0) i=fire_new(x,y,z);
		if(i<0)
		{
			world_set(x,y,z,B_FIRE_OUT);
			game_message("Too many fires burning");
			return;
		}
		fires[i].fuel=fuel_ticks(I_STICK);
	}
}

void dos_block_removed(int id,int x,int y,int z)
{
	if(B_FIRE==id || B_FIRE_OUT==id)
	{
		int i=fire_find(x,y,z);
		if(i>=0) fires[i].used=0;
		if(i==openFire) openFire=-1;
	}
}

void dos_fires_tick(void)
{
	int i;
	for(i=0; i<MAX_FIRES; ++i)
	{
		Fire *f=&fires[i];
		if(!f->used) continue;
		if(f->fuel>0) --f->fuel;
		if(f->fuel) continue;
		/* Burnt out. An unloaded column reads as bedrock, so the fire goes
		   out when its column next loads; the record stays for relighting. */
		if(B_FIRE==BLK_ID(wget(f->x,f->y,f->z)))
		{
			world_set(f->x,f->y,f->z,B_FIRE_OUT);
			if(i==openFire) g_fireLit=0;
		}
	}
}

void dos_fire_open(int x,int y,int z)
{
	openFire=fire_find(x,y,z);
	g_fireLit=B_FIRE==BLK_ID(wget(x,y,z)) && openFire>=0 && fires[openFire].fuel>0;
	if(openFire<0)
	{
		/* A fire pit with no record (or a lit fire from a full table): track it now. */
		openFire=fire_new(x,y,z);
		if(openFire>=0 && B_FIRE==BLK_ID(wget(x,y,z))) fires[openFire].fuel=1;
	}
}

void dos_fire_add_fuel(void)
{
	static const u8 order[]={I_CHARCOAL,I_COAL,B_LOG,B_PLANKS,I_STICK};
	Fire *f;
	int k,lit;
	if(openFire<0)
	{
		game_message("Too many fires burning");
		return;
	}
	f=&fires[openFire];
	lit=B_FIRE==BLK_ID(wget(f->x,f->y,f->z)) && f->fuel>0;
	if(f->fuel>=FIRE_MAX_FUEL-TICKS_PER_MINUTE)
	{
		game_message("The fire is full");
		return;
	}
	for(k=0; k<(int)sizeof(order); ++k)
	{
		/* An unlit fire also needs a stick to start it. */
		int need=lit ? 1 : (I_STICK==order[k] ? 2 : 1);
		if(inv_count(order[k])<need || (!lit && I_STICK!=order[k] && !inv_count(I_STICK))) continue;
		inv_remove(order[k],1);
		if(!lit)
		{
			inv_remove(I_STICK,1);
			world_set(f->x,f->y,f->z,B_FIRE);
		}
		f->fuel=MIN(FIRE_MAX_FUEL,f->fuel+fuel_ticks(order[k]));
		g_fireLit=1;
		sound_play(SFX_WOOD,200,200,0);
		game_message(lit ? "Fuel added" : "The fire is lit");
		return;
	}
	game_message(lit ? "No fuel: sticks, wood or charcoal" : "Need a stick and fuel to light it");
}

void dos_fire_draw_status(u8 *fb)
{
	char buf[32];
	const Fire *f=openFire>=0 ? &fires[openFire] : 0;
	if(f && g_fireLit && f->fuel)
	{
		int seconds=f->fuel/20;
		memcpy(buf,"Burns ",6);
		itoa_dec(seconds/60,buf+6);
		{
			char *p=buf+strlen(buf);
			*p++=':';
			*p++='0'+(seconds%60)/10;
			*p++='0'+seconds%10;
			*p=0;
		}
		gfx_text_shadow(fb,150,14,buf,C_WHITE,C_BLACK);
	}
	else
	{
		gfx_text_shadow(fb,150,14,"Out",C_RED,C_BLACK);
	}
	gfx_text_shadow(fb,16,212,"SPACE cook  F add fuel  ESC",P(R_GRAY,11),C_BLACK);
}

const char *dos_station_title(int station)
{
	switch(station)
	{
	case ST_FIRE: return "CAMPFIRE";
	case ST_FURNACE: return "CLAY FURNACE";
	case ST_BLOOMERY: return "BLOOMERY";
	case ST_TABLE: return "CRAFTING TABLE";
	case ST_ANVIL: return "STONE ANVIL";
	}
	return "CRAFTING";
}

void dos_extras_clear(void)
{
	memset(fires,0,sizeof(fires));
	openFire=-1;
	g_fireLit=0;
}

void dos_tool_wear(int cost)
{
	Slot *s=&g_inv[g_player.selected];
	int durability;
	if(!s->item || cost<=0) return;
	durability=g_itemDef[s->item].durability;
	if(!durability) return;
	if(s->wear+cost<durability)
	{
		s->wear+=cost;
		return;
	}
	{
		char text[40];
		const char *name=g_itemDef[s->item].name;
		int n=MIN(strlen(name),24);
		memcpy(text,name,n);
		memcpy(text+n," broke!",8);
		game_message(text);
	}
	s->item=s->count=0;
	s->wear=0;
	sound_play(SFX_CLICK,160,255,0);
}

int dos_break_drop(int id,u8 b,int drop)
{
	if(B_GRAVEL==id && B_GRAVEL==drop)
	{
		/* One roll; flint or tin uses up the gravel, so placed gravel cannot be farmed for tin. */
		if((BLK_META(b)&GRAVEL_TIN) && 0==rnd_range(100)) return I_TIN;
		if(0==rnd_range(10)) return I_FLINT;
	}
	else if(B_CLAY==id && I_CLAY_LUMP==drop)
	{
		inv_add(I_CLAY_LUMP,3);
	}
	return drop;
}

static void put_wear(void (*put)(u32),const Slot *slots,int n)
{
	int i;
	for(i=0; i<n; ++i) put(slots[i].wear);
}

static int get_wear(u32 (*get)(void),Slot *slots,int n)
{
	int i;
	for(i=0; i<n; ++i)
	{
		u32 wear=get();
		u32 durability=g_itemDef[slots[i].item].durability;
		if(wear>0xFFFF || (wear && wear>=durability)) return 0;
		slots[i].wear=(u16)wear;
	}
	return 1;
}

void dos_extras_save(void (*put)(u32))
{
	int i;
	put(EXTRAS_MAGIC);
	put_wear(put,g_inv,INV_SLOTS);
	/* Chests are written in table order and loaded compacted, in the same order. */
	for(i=0; i<MAX_CHESTS; ++i)
		if(g_chests[i].used) put_wear(put,g_chests[i].slot,CHEST_SLOTS);
	for(i=0; i<MAX_FIRES; ++i)
		if(fires[i].used)
		{
			put(1);
			put((u32)(u16)fires[i].x | (u32)fires[i].y<<16);
			put((u32)(u16)fires[i].z | (u32)fires[i].fuel<<16);
		}
	put(0);
}

int dos_extras_load(u32 (*get)(void))
{
	int i;
	if(get()!=EXTRAS_MAGIC || !get_wear(get,g_inv,INV_SLOTS)) return 0;
	for(i=0; i<MAX_CHESTS; ++i)
		if(g_chests[i].used && !get_wear(get,g_chests[i].slot,CHEST_SLOTS)) return 0;
	dos_extras_clear();
	for(i=0; ; ++i)
	{
		u32 tag=get(),a,b;
		if(0==tag) break;
		if(1!=tag || i>=MAX_FIRES) return 0;
		a=get(); b=get();
		fires[i].x=(s16)(a&0xFFFF); fires[i].y=(u8)(a>>16);
		fires[i].z=(s16)(b&0xFFFF); fires[i].fuel=(u16)(b>>16);
		if((a>>24) || fires[i].fuel>FIRE_MAX_FUEL) return 0;
		fires[i].used=1;
	}
	return 1;
}
