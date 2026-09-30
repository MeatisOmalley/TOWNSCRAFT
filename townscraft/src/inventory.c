#include "inventory.h"

Slot g_inv[INV_SLOTS];
int g_furnaceFuel;

void inv_clear(void)
{
	memset(g_inv,0,sizeof(g_inv));
	g_furnaceFuel=0;
}

int inv_add(int item,int count)
{
	int i,maxStack;
	if(item<=0 || item>=NUM_ITEMS)
	{
		return count;
	}
	maxStack=g_itemDef[item].maxStack;
	/* Existing stacks first, then empty slots (hotbar first) */
	for(i=0; i<INV_SLOTS && count>0; ++i)
	{
		if(g_inv[i].item==item && g_inv[i].count<maxStack)
		{
			int n=MIN(count,maxStack-g_inv[i].count);
			g_inv[i].count+=n;
			count-=n;
		}
	}
	for(i=0; i<INV_SLOTS && count>0; ++i)
	{
		if(0==g_inv[i].item)
		{
			int n=MIN(count,maxStack);
			g_inv[i].item=item;
			g_inv[i].count=n;
			count-=n;
		}
	}
	return count;
}

int inv_count(int item)
{
	int i,n=0;
	for(i=0; i<INV_SLOTS; ++i)
	{
		if(g_inv[i].item==item)
		{
			n+=g_inv[i].count;
		}
	}
	return n;
}

void inv_remove(int item,int count)
{
	int i;
	/* Take from storage before the hotbar */
	for(i=INV_SLOTS-1; i>=0 && count>0; --i)
	{
		if(g_inv[i].item==item)
		{
			int n=MIN(count,g_inv[i].count);
			g_inv[i].count-=n;
			count-=n;
			if(0==g_inv[i].count)
			{
				g_inv[i].item=0;
			}
		}
	}
}

void inv_take_from_slot(int slot,int count)
{
	if(g_inv[slot].count<=count)
	{
		g_inv[slot].item=0;
		g_inv[slot].count=0;
	}
	else
	{
		g_inv[slot].count-=count;
	}
}

#define R1(st,out,n,a,an) {st,out,n,{{a,an},{0,0},{0,0}}}
#define R2(st,out,n,a,an,b,bn) {st,out,n,{{a,an},{b,bn},{0,0}}}

const Recipe g_recipes[]=
{
	R1(ST_HAND,B_PLANKS,4,B_LOG,1),
	R1(ST_HAND,I_STICK,4,B_PLANKS,2),
	R1(ST_HAND,B_CRAFT_TABLE,1,B_PLANKS,4),
	R2(ST_HAND,B_TORCH,4,I_COAL,1,I_STICK,1),
	R1(ST_TABLE,B_FURNACE,1,B_COBBLE,8),
	R2(ST_TABLE,I_WOOD_PICK,1,B_PLANKS,3,I_STICK,2),
	R2(ST_TABLE,I_STONE_PICK,1,B_COBBLE,3,I_STICK,2),
	R2(ST_TABLE,I_IRON_PICK,1,I_IRON_INGOT,3,I_STICK,2),
	R2(ST_TABLE,I_WOOD_SWORD,1,B_PLANKS,2,I_STICK,1),
	R2(ST_TABLE,I_STONE_SWORD,1,B_COBBLE,2,I_STICK,1),
	R2(ST_TABLE,I_IRON_SWORD,1,I_IRON_INGOT,2,I_STICK,1),
	R2(ST_TABLE,I_WOOD_AXE,1,B_PLANKS,3,I_STICK,2),
	R2(ST_TABLE,I_STONE_AXE,1,B_COBBLE,3,I_STICK,2),
	R2(ST_TABLE,I_WOOD_SHOVEL,1,B_PLANKS,1,I_STICK,2),
	R2(ST_TABLE,I_STONE_SHOVEL,1,B_COBBLE,1,I_STICK,2),
	R1(ST_TABLE,I_DOOR,3,B_PLANKS,6),
	R1(ST_TABLE,B_CHEST,1,B_PLANKS,8),
	R2(ST_TABLE,I_BED,1,B_WOOL,3,B_PLANKS,3),
	R1(ST_TABLE,B_STONEBRICK,4,B_STONE,4),
	R2(ST_TABLE,B_TNT,1,I_GUNPOWDER,5,B_SAND,4),
	R1(ST_FURNACE,B_GLASS,1,B_SAND,1),
	R1(ST_FURNACE,I_IRON_INGOT,1,B_IRON_ORE,1),
	R1(ST_FURNACE,B_STONE,1,B_COBBLE,1),
	R1(ST_FURNACE,I_COOKED_PORK,1,I_RAW_PORK,1),
	R1(ST_FURNACE,I_COOKED_MUTTON,1,I_RAW_MUTTON,1),
	R1(ST_FURNACE,I_COAL,1,B_LOG,1),
};
const int g_numRecipes=sizeof(g_recipes)/sizeof(g_recipes[0]);

int recipe_available(const Recipe *r,int station)
{
	if(ST_FURNACE==station)
	{
		return ST_FURNACE==r->station;
	}
	return r->station==ST_HAND || (ST_TABLE==station && ST_TABLE==r->station);
}

int fuel_value(int item)
{
	switch(item)
	{
	case I_COAL: return 8;
	case B_LOG: return 3;
	case B_PLANKS: return 2;
	case I_STICK: return 1;
	}
	return 0;
}

static int has_fuel(const Recipe *r)
{
	static const u8 fuels[]={I_COAL,B_LOG,B_PLANKS,I_STICK};
	int i;
	if(ST_FURNACE!=r->station || g_furnaceFuel>0)
	{
		return 1;
	}
	for(i=0; i<(int)sizeof(fuels); ++i)
	{
		/* Do not burn the item being smelted */
		int need=(r->in[0][0]==fuels[i]) ? r->in[0][1]+1 : 1;
		if(inv_count(fuels[i])>=need)
		{
			return 1;
		}
	}
	return 0;
}

int recipe_can_craft(const Recipe *r)
{
	int i;
	for(i=0; i<3; ++i)
	{
		if(r->in[i][0] && inv_count(r->in[i][0])<r->in[i][1])
		{
			return 0;
		}
	}
	return has_fuel(r);
}

int recipe_craft(const Recipe *r)
{
	int i;
	if(!recipe_can_craft(r))
	{
		return 0;
	}
	if(ST_FURNACE==r->station && g_furnaceFuel<=0)
	{
		static const u8 fuels[]={I_COAL,B_LOG,B_PLANKS,I_STICK};
		for(i=0; i<(int)sizeof(fuels); ++i)
		{
			int need=(r->in[0][0]==fuels[i]) ? r->in[0][1]+1 : 1;
			if(inv_count(fuels[i])>=need)
			{
				inv_remove(fuels[i],1);
				g_furnaceFuel+=fuel_value(fuels[i]);
				break;
			}
		}
	}
	for(i=0; i<3; ++i)
	{
		if(r->in[i][0])
		{
			inv_remove(r->in[i][0],r->in[i][1]);
		}
	}
	if(ST_FURNACE==r->station)
	{
		--g_furnaceFuel;
	}
	inv_add(r->out,r->outCount);
	return 1;
}

/* ---------------- Chests ---------------- */

Chest g_chests[MAX_CHESTS];

void chests_clear(void)
{
	memset(g_chests,0,sizeof(g_chests));
}

Chest *chest_at(int x,int y,int z,int create)
{
	int i,freeIdx=-1;
	for(i=0; i<MAX_CHESTS; ++i)
	{
		Chest *c=&g_chests[i];
		if(c->used && c->x==x && c->y==y && c->z==z)
		{
			return c;
		}
		if(!c->used && freeIdx<0)
		{
			freeIdx=i;
		}
	}
	if(!create || freeIdx<0)
	{
		return NULL;
	}
	memset(&g_chests[freeIdx],0,sizeof(Chest));
	g_chests[freeIdx].used=1;
	g_chests[freeIdx].x=x;
	g_chests[freeIdx].y=y;
	g_chests[freeIdx].z=z;
	return &g_chests[freeIdx];
}

int chest_remove(int x,int y,int z,int toPlayer)
{
	Chest *c=chest_at(x,y,z,0);
	int i,lost=0;
	if(!c)
	{
		return 0;
	}
	for(i=0; i<CHEST_SLOTS && toPlayer; ++i)
	{
		if(c->slot[i].item)
		{
			lost+=inv_add(c->slot[i].item,c->slot[i].count);
		}
	}
	c->used=0;
	return lost;
}
