/* Blocks, items and textures. */
#ifndef BLOCKS_H
#define BLOCKS_H
#include "common.h"

/* A world cell is one byte: block id in bits 0-4, metadata in bits 5-7. */
#define BLK_ID(b)   ((b)&31)
#define BLK_META(b) ((b)>>5)
#define MKBLK(id,meta) ((u8)((id)|((meta)<<5)))

enum
{
	B_AIR,B_STONE,B_GRASS,B_DIRT,B_COBBLE,B_PLANKS,B_LOG,B_LEAVES,
	B_SAND,B_WATER,B_GLASS,B_BEDROCK,B_COAL_ORE,B_IRON_ORE,B_CRAFT_TABLE,B_FURNACE,
	B_TORCH,B_DOOR_LOWER,B_DOOR_UPPER,B_BED_FOOT,B_BED_HEAD,B_WOOL,B_GRAVEL,B_FLOWER,
	B_TALLGRASS,B_STONEBRICK,B_TNT,
	NUM_BLOCKS
};

/* Items share the id space.  Ids below 32 are the block of the same id. */
enum
{
	I_NONE=0,
	I_STICK=32,I_COAL,I_IRON_INGOT,
	I_WOOD_PICK,I_STONE_PICK,I_IRON_PICK,
	I_WOOD_SWORD,I_STONE_SWORD,I_IRON_SWORD,
	I_WOOD_AXE,I_STONE_AXE,I_WOOD_SHOVEL,I_STONE_SHOVEL,
	I_RAW_PORK,I_COOKED_PORK,I_RAW_MUTTON,I_COOKED_MUTTON,
	I_ROTTEN_FLESH,I_GUNPOWDER,I_DOOR,I_BED,I_APPLE,
	NUM_ITEMS
};

/* Block property flags */
enum
{
	BF_SOLID   =0x01,  /* Collides */
	BF_OPAQUE  =0x02,  /* Hides adjacent faces, blocks light */
	BF_CUBE    =0x04,  /* Rendered as a full cube */
	BF_SAMEHIDE=0x08,  /* Faces between two of these are hidden (glass, water) */
	BF_MODEL   =0x10,  /* Rendered with a custom model (torch, door, bed, plants) */
	BF_REPLACE =0x20,  /* Can be replaced by placing a block (air, water, plants) */
	BF_USABLE  =0x40,  /* Right-click does something */
};

/* Tool classes */
enum
{
	TOOL_NONE,TOOL_PICK,TOOL_AXE,TOOL_SHOVEL,TOOL_SWORD
};

typedef struct
{
	const char *name;
	u8 flags;
	u8 lightEmit;       /* 0-15 */
	u8 hardness;        /* Breaking time in 1/20 s with bare hands; 255 = unbreakable */
	u8 tool;            /* Preferred tool class */
	u8 needTier;        /* Minimum tool tier to get a drop (0 any, 1 wood, 2 stone) */
	u8 drop;            /* Item dropped */
	u8 tex[6];          /* Face textures -X,+X,-Y,+Y,-Z,+Z */
} BlockDef;

typedef struct
{
	const char *name;
	u8 icon;            /* Texture used as the inventory icon */
	u8 tool;            /* Tool class */
	u8 tier;            /* 1 wood, 2 stone, 3 iron */
	u8 damage;          /* Melee damage in half hearts */
	u8 food;            /* Health restored when eaten (half hearts) */
	u8 maxStack;
	u8 placeBlock;      /* Block placed, 0 if none */
} ItemDef;

extern const BlockDef g_blockDef[NUM_BLOCKS];
extern ItemDef g_itemDef[NUM_ITEMS];
void items_init(void);

int block_box(u8 b,u8 *lo,u8 *hi);   /* Collision box in 1/16 units, 0 if none */
int door_side(u8 b);

static inline u8 blk_flags(u8 b){return g_blockDef[BLK_ID(b)].flags;}

/* Face directions */
enum
{
	DIR_NX,DIR_PX,DIR_NY,DIR_PY,DIR_NZ,DIR_PZ
};

/* Textures (16x16, palette indices, 0 = transparent) */
enum
{
	T_STONE,T_GRASS_TOP,T_GRASS_SIDE,T_DIRT,T_COBBLE,T_PLANKS,T_LOG_SIDE,T_LOG_TOP,
	T_LEAVES,T_SAND,T_WATER,T_GLASS,T_BEDROCK,T_COAL_ORE,T_IRON_ORE,T_CRAFT_TOP,
	T_CRAFT_SIDE,T_CRAFT_FRONT,T_FURNACE_FRONT,T_FURNACE_SIDE,T_FURNACE_TOP,T_TORCH,T_DOOR_BOTTOM,T_DOOR_TOP,
	T_BED_FOOT_TOP,T_BED_HEAD_TOP,T_BED_SIDE,T_BED_END,T_WOOL,T_GRAVEL,T_FLOWER,T_TALLGRASS,
	T_STONEBRICK,T_TNT_SIDE,T_TNT_TOP,T_BED_HEAD_SIDE,
	/* Mobs */
	T_PIG_SKIN,T_PIG_FACE,T_SHEEP_WOOL,T_SHEEP_FACE,T_ZOMBIE_SKIN,T_ZOMBIE_FACE,T_SHIRT,T_PANTS,
	T_CREEPER_SKIN,T_CREEPER_FACE,T_PLAYER_SKIN,
	/* Item icons */
	T_I_STICK,T_I_COAL,T_I_IRON,
	T_I_WOOD_PICK,T_I_STONE_PICK,T_I_IRON_PICK,T_I_WOOD_SWORD,T_I_STONE_SWORD,T_I_IRON_SWORD,
	T_I_WOOD_AXE,T_I_STONE_AXE,T_I_WOOD_SHOVEL,T_I_STONE_SHOVEL,T_I_PORK,
	T_I_COOKED_PORK,T_I_MUTTON,T_I_COOKED_MUTTON,T_I_FLESH,T_I_GUNPOWDER,T_I_DOOR,T_I_BED,T_I_APPLE,
	T_I_TORCH,
	/* Misc */
	T_CRACK,
	NUM_TEXTURES
};

#endif
