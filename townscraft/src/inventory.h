#ifndef INVENTORY_H
#define INVENTORY_H
#include "blocks.h"

#define INV_SLOTS 36      /* 0-8 hotbar, 9-35 storage */
#define HOTBAR 9

typedef struct
{
	u8 item,count;
} Slot;

extern Slot g_inv[INV_SLOTS];

void inv_clear(void);
int inv_add(int item,int count);       /* Returns the number that did not fit */
int inv_count(int item);
void inv_remove(int item,int count);
void inv_take_from_slot(int slot,int count);

/* Chests: contents by position */
#define MAX_CHESTS 64
#define CHEST_SLOTS 27
typedef struct
{
	u8 used,x,y,z;
	Slot slot[CHEST_SLOTS];
} Chest;
extern Chest g_chests[MAX_CHESTS];
void chests_clear(void);
Chest *chest_at(int x,int y,int z,int create);   /* NULL if none (or all used) */
int chest_remove(int x,int y,int z,int toPlayer); /* Items that did not fit */

/* Crafting */
enum
{
	ST_HAND,ST_TABLE,ST_FURNACE
};

typedef struct
{
	u8 station;
	u8 out,outCount;
	u8 in[3][2];          /* item,count */
} Recipe;

extern const Recipe g_recipes[];
extern const int g_numRecipes;
extern int g_furnaceFuel;              /* Smelting operations banked */

int recipe_available(const Recipe *r,int station);  /* Shown at this station */
int recipe_can_craft(const Recipe *r);
int recipe_craft(const Recipe *r);     /* 1 if crafted */
int fuel_value(int item);

#endif
