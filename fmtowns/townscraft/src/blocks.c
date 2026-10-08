#include "blocks.h"

#define SIDES(t) t,t,t,t,t,t
#define CUBEOPQ (BF_SOLID|BF_OPAQUE|BF_CUBE)

const BlockDef g_blockDef[NUM_BLOCKS]=
{
	/* name        flags                              light hard tool      tier drop           tex */
	{"Air",        BF_REPLACE,                         0,  0,  TOOL_NONE,  0, 0,             {SIDES(0)}},
	{"Stone",      CUBEOPQ,                            0, 30,  TOOL_PICK,  1, B_COBBLE,      {SIDES(T_STONE)}},
	{"Grass",      CUBEOPQ,                            0, 12,  TOOL_SHOVEL,0, B_DIRT,        {T_GRASS_SIDE,T_GRASS_SIDE,T_DIRT,T_GRASS_TOP,T_GRASS_SIDE,T_GRASS_SIDE}},
	{"Dirt",       CUBEOPQ,                            0, 10,  TOOL_SHOVEL,0, B_DIRT,        {SIDES(T_DIRT)}},
	{"Cobblestone",CUBEOPQ,                            0, 40,  TOOL_PICK,  1, B_COBBLE,      {SIDES(T_COBBLE)}},
	{"Planks",     CUBEOPQ,                            0, 30,  TOOL_AXE,   0, B_PLANKS,      {SIDES(T_PLANKS)}},
	{"Log",        CUBEOPQ,                            0, 35,  TOOL_AXE,   0, B_LOG,         {T_LOG_SIDE,T_LOG_SIDE,T_LOG_TOP,T_LOG_TOP,T_LOG_SIDE,T_LOG_SIDE}},
	{"Leaves",     CUBEOPQ,                            0,  5,  TOOL_NONE,  0, 0,             {SIDES(T_LEAVES)}},
	{"Sand",       CUBEOPQ,                            0, 10,  TOOL_SHOVEL,0, B_SAND,        {SIDES(T_SAND)}},
	{"Water",      BF_SAMEHIDE|BF_CUBE|BF_REPLACE,     0,255,  TOOL_NONE,  0, 0,             {SIDES(T_WATER)}},
	{"Glass",      BF_SOLID|BF_CUBE|BF_SAMEHIDE,       0,  6,  TOOL_NONE,  0, 0,             {SIDES(T_GLASS)}},
	{"Bedrock",    CUBEOPQ,                            0,255,  TOOL_NONE,  0, 0,             {SIDES(T_BEDROCK)}},
	{"Coal Ore",   CUBEOPQ,                            0, 45,  TOOL_PICK,  1, I_COAL,        {SIDES(T_COAL_ORE)}},
	{"Iron Ore",   CUBEOPQ,                            0, 50,  TOOL_PICK,  2, B_IRON_ORE,    {SIDES(T_IRON_ORE)}},
	{"Crafting Table",CUBEOPQ|BF_USABLE,               0, 35,  TOOL_AXE,   0, B_CRAFT_TABLE, {T_CRAFT_SIDE,T_CRAFT_SIDE,T_PLANKS,T_CRAFT_TOP,T_CRAFT_FRONT,T_CRAFT_FRONT}},
	{"Furnace",    CUBEOPQ|BF_USABLE,                  0, 50,  TOOL_PICK,  1, B_FURNACE,     {T_FURNACE_SIDE,T_FURNACE_SIDE,T_FURNACE_TOP,T_FURNACE_TOP,T_FURNACE_FRONT,T_FURNACE_FRONT}},
	{"Torch",      BF_MODEL,                          14,  1,  TOOL_NONE,  0, B_TORCH,       {SIDES(T_TORCH)}},
	{"Door",       BF_MODEL|BF_SOLID|BF_USABLE,        0, 30,  TOOL_AXE,   0, I_DOOR,        {SIDES(T_DOOR_BOTTOM)}},
	{"Door",       BF_MODEL|BF_SOLID|BF_USABLE,        0, 30,  TOOL_AXE,   0, I_DOOR,        {SIDES(T_DOOR_TOP)}},
	{"Bed",        BF_MODEL|BF_SOLID|BF_USABLE,        0,  5,  TOOL_NONE,  0, I_BED,         {T_BED_SIDE,T_BED_SIDE,T_PLANKS,T_BED_FOOT_TOP,T_BED_END,T_BED_END}},
	{"Bed",        BF_MODEL|BF_SOLID|BF_USABLE,        0,  5,  TOOL_NONE,  0, I_BED,         {T_BED_HEAD_SIDE,T_BED_HEAD_SIDE,T_PLANKS,T_BED_HEAD_TOP,T_BED_END,T_BED_END}},
	{"Wool",       CUBEOPQ,                            0, 12,  TOOL_NONE,  0, B_WOOL,        {SIDES(T_WOOL)}},
	{"Gravel",     CUBEOPQ,                            0, 12,  TOOL_SHOVEL,0, B_GRAVEL,      {SIDES(T_GRAVEL)}},
	{"Flower",     BF_MODEL|BF_REPLACE,                0,  1,  TOOL_NONE,  0, B_FLOWER,      {SIDES(T_FLOWER)}},
	{"Tall Grass", BF_MODEL|BF_REPLACE,                0,  1,  TOOL_NONE,  0, 0,             {SIDES(T_TALLGRASS)}},
	{"Stone Bricks",CUBEOPQ,                           0, 45,  TOOL_PICK,  1, B_STONEBRICK,  {SIDES(T_STONEBRICK)}},
	{"TNT",        CUBEOPQ|BF_USABLE,                  0,  1,  TOOL_NONE,  0, B_TNT,         {T_TNT_SIDE,T_TNT_SIDE,T_TNT_TOP,T_TNT_TOP,T_TNT_SIDE,T_TNT_SIDE}},
	{"Chest",      CUBEOPQ|BF_USABLE,                  0, 35,  TOOL_AXE,   0, B_CHEST,       {T_CHEST_FRONT,T_CHEST_FRONT,T_CHEST_TOP,T_CHEST_TOP,T_CHEST_FRONT,T_CHEST_FRONT}},
	{"Gold Ore",   CUBEOPQ,                            0, 55,  TOOL_PICK,  3, B_GOLD_ORE,    {SIDES(T_GOLD_ORE)}},
	{"Diamond Ore",CUBEOPQ,                            0, 60,  TOOL_PICK,  3, I_DIAMOND,     {SIDES(T_DIAMOND_ORE)}},
};

ItemDef g_itemDef[NUM_ITEMS];

static void def_item(int id,const char *name,int icon,int tool,int tier,int damage,int food,int maxStack,int place)
{
	ItemDef *d=&g_itemDef[id];
	d->name=name;
	d->icon=icon;
	d->tool=tool;
	d->tier=tier;
	d->damage=damage;
	d->food=food;
	d->maxStack=maxStack;
	d->placeBlock=place;
}

void items_init(void)
{
	int i;
	for(i=1; i<NUM_BLOCKS; ++i)
	{
		def_item(i,g_blockDef[i].name,g_blockDef[i].tex[DIR_PZ],TOOL_NONE,0,1,0,64,i);
	}
	g_itemDef[B_GRASS].icon=T_GRASS_SIDE;
	g_itemDef[B_TORCH].icon=T_I_TORCH;
	g_itemDef[B_LOG].icon=T_LOG_SIDE;
	g_itemDef[B_FURNACE].icon=T_FURNACE_FRONT;
	g_itemDef[B_CRAFT_TABLE].icon=T_CRAFT_FRONT;
	g_itemDef[B_DOOR_LOWER].placeBlock=0;
	g_itemDef[B_DOOR_UPPER].placeBlock=0;
	g_itemDef[B_BED_FOOT].placeBlock=0;
	g_itemDef[B_BED_HEAD].placeBlock=0;
	g_itemDef[B_WATER].placeBlock=0;

	def_item(I_STICK,       "Stick",         T_I_STICK,       TOOL_NONE,  0,1,0,64,0);
	def_item(I_COAL,        "Coal",          T_I_COAL,        TOOL_NONE,  0,1,0,64,0);
	def_item(I_IRON_INGOT,  "Iron Ingot",    T_I_IRON,        TOOL_NONE,  0,1,0,64,0);
	def_item(I_WOOD_PICK,   "Wooden Pickaxe",T_I_WOOD_PICK,   TOOL_PICK,  1,2,0,1,0);
	def_item(I_STONE_PICK,  "Stone Pickaxe", T_I_STONE_PICK,  TOOL_PICK,  2,3,0,1,0);
	def_item(I_IRON_PICK,   "Iron Pickaxe",  T_I_IRON_PICK,   TOOL_PICK,  3,4,0,1,0);
	def_item(I_WOOD_SWORD,  "Wooden Sword",  T_I_WOOD_SWORD,  TOOL_SWORD, 1,4,0,1,0);
	def_item(I_STONE_SWORD, "Stone Sword",   T_I_STONE_SWORD, TOOL_SWORD, 2,5,0,1,0);
	def_item(I_IRON_SWORD,  "Iron Sword",    T_I_IRON_SWORD,  TOOL_SWORD, 3,6,0,1,0);
	def_item(I_WOOD_AXE,    "Wooden Axe",    T_I_WOOD_AXE,    TOOL_AXE,   1,3,0,1,0);
	def_item(I_STONE_AXE,   "Stone Axe",     T_I_STONE_AXE,   TOOL_AXE,   2,4,0,1,0);
	def_item(I_WOOD_SHOVEL, "Wooden Shovel", T_I_WOOD_SHOVEL, TOOL_SHOVEL,1,2,0,1,0);
	def_item(I_STONE_SHOVEL,"Stone Shovel",  T_I_STONE_SHOVEL,TOOL_SHOVEL,2,2,0,1,0);
	def_item(I_RAW_PORK,    "Raw Porkchop",  T_I_PORK,        TOOL_NONE,  0,1,3,64,0);
	def_item(I_COOKED_PORK, "Cooked Porkchop",T_I_COOKED_PORK,TOOL_NONE,  0,1,8,64,0);
	def_item(I_RAW_MUTTON,  "Raw Mutton",    T_I_MUTTON,      TOOL_NONE,  0,1,2,64,0);
	def_item(I_COOKED_MUTTON,"Cooked Mutton",T_I_COOKED_MUTTON,TOOL_NONE, 0,1,6,64,0);
	def_item(I_ROTTEN_FLESH,"Rotten Flesh",  T_I_FLESH,       TOOL_NONE,  0,1,1,64,0);
	def_item(I_GUNPOWDER,   "Gunpowder",     T_I_GUNPOWDER,   TOOL_NONE,  0,1,0,64,0);
	def_item(I_DOOR,        "Door",          T_I_DOOR,        TOOL_NONE,  0,1,0,16,0);
	def_item(I_BED,         "Bed",           T_I_BED,         TOOL_NONE,  0,1,0,1,0);
	def_item(I_APPLE,       "Apple",         T_I_APPLE,       TOOL_NONE,  0,1,4,64,0);
	def_item(I_GOLD_INGOT,  "Gold Ingot",    T_I_GOLD,        TOOL_NONE,  0,1,0,64,0);
	def_item(I_DIAMOND,     "Diamond",       T_I_DIAMOND,     TOOL_NONE,  0,1,0,64,0);
	def_item(I_DIAMOND_PICK,"Diamond Pickaxe",T_I_DIAMOND_PICK,TOOL_PICK, 4,5,0,1,0);
	def_item(I_DIAMOND_SWORD,"Diamond Sword",T_I_DIAMOND_SWORD,TOOL_SWORD,4,8,0,1,0);
	def_item(I_GOLDEN_APPLE,"Golden Apple",  T_I_GOLDEN_APPLE,TOOL_NONE,  0,1,20,64,0);
}

/* Door geometry: meta bits 0-1 give the side of the cell the closed door
   sits on (0 -X, 1 +X, 2 -Z, 3 +Z), bit 2 means open (swung to another side). */
static const u8 doorLo[4][3]={{0,0,0},{13,0,0},{0,0,0},{0,0,13}};
static const u8 doorHi[4][3]={{3,16,16},{16,16,16},{16,16,3},{16,16,16}};
static const u8 doorOpenSide[4]={2,3,1,0};

int door_side(u8 b)
{
	int meta=BLK_META(b),side=meta&3;
	if(meta&4)
	{
		side=doorOpenSide[side];
	}
	return side;
}

int block_box(u8 b,u8 *lo,u8 *hi)
{
	int id=BLK_ID(b),k;
	const BlockDef *d=&g_blockDef[id];
	if(!(d->flags&BF_SOLID))
	{
		return 0;
	}
	switch(id)
	{
	case B_DOOR_LOWER:
	case B_DOOR_UPPER:
		for(k=0; k<3; ++k)
		{
			lo[k]=doorLo[door_side(b)][k];
			hi[k]=doorHi[door_side(b)][k];
		}
		return 1;
	case B_BED_FOOT:
	case B_BED_HEAD:
		lo[0]=lo[1]=lo[2]=0;
		hi[0]=hi[2]=16;
		hi[1]=9;
		return 1;
	}
	lo[0]=lo[1]=lo[2]=0;
	hi[0]=hi[1]=hi[2]=16;
	return 1;
}
