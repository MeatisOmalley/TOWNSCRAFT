/* DOS-only compact crafting grid. The pinned Towns recipe costs and outputs
 * remain authoritative; each occupied pattern cell consumes one item. */
#include "craft_grid.h"

Slot dos_craft_grid[9];

typedef struct {
    unsigned char recipe;
    unsigned char cell[9];
} CraftPattern;

#define P(n,a,b,c,d,e,f,g,h,i) {n,{a,b,c,d,e,f,g,h,i}}
static const CraftPattern patterns[] = {
    P( 0,B_LOG,0,0, 0,0,0, 0,0,0),
    P( 1,B_PLANKS,0,0, B_PLANKS,0,0, 0,0,0),
    P( 2,B_PLANKS,B_PLANKS,0, B_PLANKS,B_PLANKS,0, 0,0,0),
    P( 3,I_COAL,0,0, I_STICK,0,0, 0,0,0),
    P( 4,B_COBBLE,B_COBBLE,B_COBBLE, B_COBBLE,0,B_COBBLE, B_COBBLE,B_COBBLE,B_COBBLE),
    P( 5,B_PLANKS,B_PLANKS,B_PLANKS, 0,I_STICK,0, 0,I_STICK,0),
    P( 6,B_COBBLE,B_COBBLE,B_COBBLE, 0,I_STICK,0, 0,I_STICK,0),
    P( 7,I_IRON_INGOT,I_IRON_INGOT,I_IRON_INGOT, 0,I_STICK,0, 0,I_STICK,0),
    P( 8,0,B_PLANKS,0, 0,B_PLANKS,0, 0,I_STICK,0),
    P( 9,0,B_COBBLE,0, 0,B_COBBLE,0, 0,I_STICK,0),
    P(10,0,I_IRON_INGOT,0, 0,I_IRON_INGOT,0, 0,I_STICK,0),
    P(11,B_PLANKS,B_PLANKS,0, B_PLANKS,I_STICK,0, 0,I_STICK,0),
    P(12,B_COBBLE,B_COBBLE,0, B_COBBLE,I_STICK,0, 0,I_STICK,0),
    P(13,0,B_PLANKS,0, 0,I_STICK,0, 0,I_STICK,0),
    P(14,0,B_COBBLE,0, 0,I_STICK,0, 0,I_STICK,0),
    P(15,B_PLANKS,B_PLANKS,0, B_PLANKS,B_PLANKS,0, B_PLANKS,B_PLANKS,0),
    P(16,B_PLANKS,B_PLANKS,B_PLANKS, B_PLANKS,0,B_PLANKS, B_PLANKS,B_PLANKS,B_PLANKS),
    P(17,I_DIAMOND,I_DIAMOND,I_DIAMOND, 0,I_STICK,0, 0,I_STICK,0),
    P(18,0,I_DIAMOND,0, 0,I_DIAMOND,0, 0,I_STICK,0),
    P(19,0,I_GOLD_INGOT,0, I_GOLD_INGOT,I_APPLE,I_GOLD_INGOT, 0,I_GOLD_INGOT,0),
    P(20,B_WOOL,B_WOOL,B_WOOL, B_PLANKS,B_PLANKS,B_PLANKS, 0,0,0),
    P(21,B_STONE,B_STONE,0, B_STONE,B_STONE,0, 0,0,0),
    P(22,I_GUNPOWDER,B_SAND,I_GUNPOWDER, B_SAND,I_GUNPOWDER,B_SAND, I_GUNPOWDER,B_SAND,I_GUNPOWDER)
};
#undef P

void dos_craft_grid_clear(void)
{
    memset(dos_craft_grid,0,sizeof(dos_craft_grid));
}

static int pattern_matches(const CraftPattern *p,int width,int shift_x,int shift_y,int mirror)
{
    int x,y,min_x=3,min_y=3,max_x=-1,max_y=-1;
    for(y=0;y<3;++y) for(x=0;x<3;++x) if(p->cell[y*3+x]) {
        if(x<min_x) min_x=x;
        if(x>max_x) max_x=x;
        if(y<min_y) min_y=y;
        if(y>max_y) max_y=y;
    }
    if(max_x<0 || max_x-min_x>=width || max_y-min_y>=width) return 0;
    if(shift_x+max_x-min_x>=width || shift_y+max_y-min_y>=width) return 0;
    for(y=0;y<width;++y) for(x=0;x<width;++x) {
        int px=x-shift_x,py=y-shift_y,expected=0;
        int actual=dos_craft_grid[y*3+x].item;
        if(px>=0 && py>=0 && px<=max_x-min_x && py<=max_y-min_y) {
            expected=p->cell[(py+min_y)*3+(mirror ? max_x-px : min_x+px)];
        }
        if(actual!=expected) return 0;
    }
    return 1;
}

const Recipe *dos_craft_grid_match(int station,int width)
{
    unsigned int i;
    if(width!=2 && width!=3) return 0;
    for(i=0;i<sizeof(patterns)/sizeof(patterns[0]);++i) {
        const CraftPattern *p=&patterns[i];
        const Recipe *r=&g_recipes[p->recipe];
        int x,y,mirror;
        if(!recipe_available(r,station)) continue;
        for(y=0;y<width;++y) for(x=0;x<width;++x)
            for(mirror=0;mirror<2;++mirror)
                if(pattern_matches(p,width,x,y,mirror)) return r;
    }
    return 0;
}

int dos_craft_grid_take(Slot *destination,int station,int width)
{
    const Recipe *r=dos_craft_grid_match(station,width);
    int x,y;
    if(!r || !destination) return 0;
    if(destination->item && destination->item!=r->out) return 0;
    if(destination->count+r->outCount>g_itemDef[r->out].maxStack) return 0;
    for(y=0;y<width;++y) for(x=0;x<width;++x) {
        Slot *s=&dos_craft_grid[y*3+x];
        if(s->item && --s->count==0) s->item=0;
    }
    if(!destination->item) destination->wear=0;
    destination->item=r->out;
    destination->count+=r->outCount;
    return 1;
}

int dos_craft_grid_return(void)
{
    int i,all=1;
    for(i=0;i<9;++i) {
        Slot *s=&dos_craft_grid[i];
        if(s->item) {
            s->count=inv_add_slot(s);
            if(s->count) all=0;
            else s->item=0;
        }
    }
    return all;
}
