#include <stdio.h>
#include "craft_grid.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

/* The pattern table and matching code are the production implementation. */
#include "../src/platform/dos/craft_grid.c"

ItemDef g_itemDef[NUM_ITEMS];

static void fill_pattern(unsigned int index)
{
    int i;
    dos_craft_grid_clear();
    for(i=0;i<9;++i) if(patterns[index].cell[i]) {
        dos_craft_grid[i].item=patterns[index].cell[i];
        dos_craft_grid[i].count=1;
    }
}

int main(void)
{
    unsigned int i;
    int j,k;
    for(i=1;i<NUM_ITEMS;++i) g_itemDef[i].maxStack=64;
    for(i=0;i<sizeof(patterns)/sizeof(patterns[0]);++i) {
        const Recipe *r=&g_recipes[patterns[i].recipe];
        int counts[NUM_ITEMS]={0};
        Slot output={0,0};
        for(j=0;j<9;++j) if(patterns[i].cell[j]) ++counts[patterns[i].cell[j]];
        for(j=0;j<3;++j) if(r->in[j][0]) {
            CHECK(counts[r->in[j][0]]==r->in[j][1]);
            counts[r->in[j][0]]=0;
        }
        for(j=0;j<NUM_ITEMS;++j) CHECK(counts[j]==0);
        fill_pattern(i);
        CHECK(dos_craft_grid_match(r->station,r->station==ST_HAND ? 2 : 3)==r);
        CHECK(dos_craft_grid_take(&output,r->station,r->station==ST_HAND ? 2 : 3));
        CHECK(output.item==r->out && output.count==r->outCount);
        for(k=0;k<9;++k) CHECK(dos_craft_grid[k].item==0);
        if(r->station==ST_TABLE) {
            fill_pattern(i);
            CHECK(dos_craft_grid_match(ST_HAND,2)!=r);
        }
    }
    fill_pattern(0);
    CHECK(dos_craft_grid_match(ST_HAND,2));
    dos_craft_grid[1].item=B_COBBLE;
    dos_craft_grid[1].count=1;
    CHECK(dos_craft_grid_match(ST_HAND,2)==0);
    fill_pattern(5);
    {
        Slot full={I_WOOD_PICK,64};
        CHECK(!dos_craft_grid_take(&full,ST_TABLE,3));
        CHECK(dos_craft_grid[0].count==1 && dos_craft_grid[4].count==1);
    }
    dos_craft_grid_clear();
    /* A horizontally mirrored, shifted axe should still be recognized. */
    dos_craft_grid[1].item=B_PLANKS; dos_craft_grid[1].count=1;
    dos_craft_grid[2].item=B_PLANKS; dos_craft_grid[2].count=1;
    dos_craft_grid[5].item=B_PLANKS; dos_craft_grid[5].count=1;
    dos_craft_grid[4].item=I_STICK; dos_craft_grid[4].count=1;
    dos_craft_grid[7].item=I_STICK; dos_craft_grid[7].count=1;
    CHECK(dos_craft_grid_match(ST_TABLE,3)==&g_recipes[11]);
    inv_clear();
    dos_craft_grid_clear();
    dos_craft_grid[0].item=B_LOG;
    dos_craft_grid[0].count=3;
    CHECK(dos_craft_grid_return());
    CHECK(inv_count(B_LOG)==3 && dos_craft_grid[0].item==0);
    for(j=0;j<INV_SLOTS;++j) { g_inv[j].item=B_STONE; g_inv[j].count=64; }
    dos_craft_grid[0].item=B_LOG;
    dos_craft_grid[0].count=2;
    CHECK(!dos_craft_grid_return());
    CHECK(dos_craft_grid[0].item==B_LOG && dos_craft_grid[0].count==2);
    g_inv[0].item=B_LOG;
    g_inv[0].count=63;
    CHECK(!dos_craft_grid_return());
    CHECK(g_inv[0].count==64 && dos_craft_grid[0].count==1);
    puts("PASS: all crafting patterns and safe grid return");
    return 0;
}
