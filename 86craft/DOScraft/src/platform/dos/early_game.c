/* DOS early-game systems: tool wear and the save extension section. */
#include "early_game.h"
#include "player.h"
#include "game.h"
#include "sound.h"

#define EXTRAS_MAGIC 0x31584745u /* "EGX1" */

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
	put(0);
}

int dos_extras_load(u32 (*get)(void))
{
	int i;
	if(get()!=EXTRAS_MAGIC || !get_wear(get,g_inv,INV_SLOTS)) return 0;
	for(i=0; i<MAX_CHESTS; ++i)
		if(g_chests[i].used && !get_wear(get,g_chests[i].slot,CHEST_SLOTS)) return 0;
	return 0==get();
}
