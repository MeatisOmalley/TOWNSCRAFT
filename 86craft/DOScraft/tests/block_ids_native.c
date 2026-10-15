#include <stdio.h>
#include "blocks.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

int door_side(u8 b);

/* The Towns three-bit door side: bits 0-1 the side, bit 2 open. */
static int towns_door_side(int meta)
{
    static const u8 openSide[4]={2,3,1,0};
    return (meta&4) ? openSide[meta&3] : (meta&3);
}

int main(void)
{
    int b,id,meta,lower;
    CHECK(NUM_BLOCKS<=BLOCK_IDS);
    CHECK(I_STICK==BLOCK_IDS);
    CHECK(NUM_ITEMS<=256);
    /* Every cell byte splits and rebuilds exactly. */
    for(b=0;b<256;++b) {
        CHECK(BLK_ID(b)<BLOCK_IDS && BLK_META(b)<4);
        CHECK(MKBLK(BLK_ID(b),BLK_META(b))==b);
    }
    /* Baked terrain never sets bit 5, so its bytes keep their meaning. */
    for(id=0;id<B_WALL_TORCH;++id) CHECK(BLK_ID(id)==id && BLK_META(id)==0);
    CHECK(BLK_ID(B_LEAVES|64)==B_LEAVES && BLK_META(B_LEAVES|64)==1);
    CHECK(B_WALL_TORCH==30 && B_DOOR_LOWER_OPEN==31 && B_DOOR_UPPER_OPEN==32);
    /* Doors keep all eight Towns states and the same geometry side. */
    for(lower=0;lower<2;++lower) for(meta=0;meta<8;++meta) {
        u8 cell=DOOR_BLK(lower,meta);
        CHECK(DOOR_META(cell)==meta);
        CHECK(door_is_lower(BLK_ID(cell))==lower && door_is_upper(BLK_ID(cell))==!lower);
        CHECK(door_side(cell)==towns_door_side(meta));
    }
    for(meta=0;meta<4;++meta) CHECK(BLK_META(MKBLK(B_WALL_TORCH,meta))==meta);
    /* Split-out blocks behave as the originals. */
    CHECK(g_blockDef[B_WALL_TORCH].lightEmit==g_blockDef[B_TORCH].lightEmit);
    CHECK(g_blockDef[B_WALL_TORCH].drop==B_TORCH && g_blockDef[B_WALL_TORCH].flags==g_blockDef[B_TORCH].flags);
    CHECK(g_blockDef[B_DOOR_LOWER_OPEN].flags==g_blockDef[B_DOOR_LOWER].flags);
    CHECK(g_blockDef[B_DOOR_UPPER_OPEN].flags==g_blockDef[B_DOOR_UPPER].flags);
    CHECK(g_blockDef[B_DOOR_LOWER_OPEN].drop==I_DOOR && g_blockDef[B_DOOR_UPPER_OPEN].drop==I_DOOR);
    /* Unused IDs read as empty air-like entries, never past the table. */
    for(id=NUM_BLOCKS;id<BLOCK_IDS;++id) CHECK(g_blockDef[id].flags==0);
    items_init();
    CHECK(g_itemDef[B_WALL_TORCH].placeBlock==0);
    CHECK(g_itemDef[B_DOOR_LOWER_OPEN].placeBlock==0 && g_itemDef[B_DOOR_UPPER_OPEN].placeBlock==0);
    CHECK(g_itemDef[B_TORCH].placeBlock==B_TORCH && g_itemDef[I_STICK].maxStack==64);
    printf("PASS: 6-bit block ids\n");
    return 0;
}
