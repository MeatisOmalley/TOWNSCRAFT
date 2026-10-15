#include <stdio.h>
#include "early_game.h"
#include "player.h"

#include "world.h"
#include "gfx.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

/* A 16x16 world in the flat (non-column) layout. */
int g_W=16,g_NC=1;
u8 *g_blocks;
int *g_zOff;
short *g_columnMap;
static u8 cells[16*16*WH];
static int zOff[16];
void world_set(int x,int y,int z,u8 b) { g_blocks[widx(x,y,z)]=b; }
void gfx_text_shadow(u8 *fb,int x,int y,const char *s,u8 c,u8 shadow) { (void)fb;(void)x;(void)y;(void)s;(void)c;(void)shadow; }

Player g_player;

/* Mobs and aiming, as stubs the spear code can be driven through. */
#include "mobs.h"
Mob g_mobs[MAX_MOBS];
static int rayMob=-1,rayDist,hurtMob=-1,hurtDamage,hurts;
int mobs_ray_hit(int ex,int ey,int ez,int dx,int dy,int dz,int maxDist,int *dist)
{ (void)ex;(void)ey;(void)ez;(void)dx;(void)dy;(void)dz; *dist=rayDist; return rayDist<=maxDist ? rayMob : -1; }
void mob_hurt(int i,int dmg,int fromX,int fromZ,int byPlayer) { (void)fromX;(void)fromZ;(void)byPlayer; hurtMob=i; hurtDamage=dmg; ++hurts; }
void player_eye(int *x,int *y,int *z) { *x=g_player.body.x; *y=g_player.body.y; *z=g_player.body.z; }
void player_look_dir(int *dx,int *dy,int *dz) { *dx=0; *dy=0; *dz=16384; }   /* Looking along +Z */
u32 isqrt(u32 v) { u32 r=0; while((r+1)*(r+1)<=v) ++r; return r; }
static char lastMessage[64];
static int sounds;
void game_message(const char *msg) { int i=0; while(msg[i] && i<63) { lastMessage[i]=msg[i]; ++i; } lastMessage[i]=0; }
static int contains(const char *s,const char *w) { for(;*s;++s) { int i=0; while(w[i] && s[i]==w[i]) ++i; if(!w[i]) return 1; } return 0; }
void sound_play(int sfx,int pitch,int vol,int pan) { (void)sfx;(void)pitch;(void)vol;(void)pan;++sounds; }

static u32 wire[4096];
static int count,position;
static void put(u32 v) { wire[count++]=v; }
static u32 get(void) { return position<count ? wire[position++] : 0xFFFFFFFFu; }

int main(void)
{
    int i,uses=g_itemDef[I_STONE_PICK].durability;
    items_init();
    uses=g_itemDef[I_STONE_PICK].durability;
    CHECK(uses==130 && g_itemDef[I_IRON_PICK].durability==600 && !g_itemDef[I_STICK].durability);
    /* A held tool wears and breaks on its last use; other items never wear. */
    inv_clear();
    inv_add(I_STONE_PICK,1); inv_add(I_STICK,5);
    g_player.selected=0;
    for(i=0;i<uses-1;++i) dos_tool_wear(1);
    CHECK(g_inv[0].item==I_STONE_PICK && g_inv[0].wear==uses-1);
    dos_tool_wear(1);
    CHECK(!g_inv[0].item && !g_inv[0].wear && sounds==1 && contains(lastMessage,"broke"));
    g_player.selected=1; dos_tool_wear(2);
    CHECK(g_inv[1].item==I_STICK && g_inv[1].count==5 && !g_inv[1].wear);
    /* Worn tools keep their wear when moved; a fresh slot starts unworn. */
    {
        Slot worn={I_IRON_PICK,1,77};
        CHECK(inv_add_slot(&worn)==0 && g_inv[0].item==I_IRON_PICK && g_inv[0].wear==77);
        g_inv[0].item=0; g_inv[0].count=0;   /* Left with stale wear */
        inv_add(I_IRON_PICK,1);
        CHECK(g_inv[0].item==I_IRON_PICK && g_inv[0].wear==0);
        for(i=0;i<INV_SLOTS;++i) if(!g_inv[i].item) { g_inv[i].item=B_DIRT; g_inv[i].count=64; }
        CHECK(inv_add_slot(&worn)==1);
    }
    /* The extension section round-trips wear for inventory and used chests. */
    inv_clear(); chests_clear();
    inv_add(I_IRON_SWORD,1); g_inv[0].wear=321;
    {
        Chest *a=chest_at(1,2,3,1),*b=chest_at(4,5,6,1);
        CHECK(a && b);
        a->slot[3].item=I_DIAMOND_PICK; a->slot[3].count=1; a->slot[3].wear=1499;
        b->slot[0].item=I_STONE_AXE; b->slot[0].count=1; b->slot[0].wear=5;
    }
    count=position=0;
    dos_extras_save(put);
    g_inv[0].wear=0; g_chests[0].slot[3].wear=0; g_chests[1].slot[0].wear=0;
    CHECK(dos_extras_load(get) && position==count);
    CHECK(g_inv[0].wear==321 && g_chests[0].slot[3].wear==1499 && g_chests[1].slot[0].wear==5);
    /* Corrupt sections are rejected: worn-out wear, bad magic, missing end. */
    position=0; wire[1]=600; CHECK(!dos_extras_load(get));
    wire[1]=321; position=0; wire[0]^=1; CHECK(!dos_extras_load(get));
    wire[0]^=1; position=0; --count; CHECK(!dos_extras_load(get));
    printf("PASS: tool wear and save extension\n");
    /* Materials: what hands and tools get, and the new tiers. */
    CHECK(B_CLAY==33 && B_BLOOMERY==40 && NUM_BLOCKS<=BLOCK_IDS && I_BELLOWS<NUM_ITEMS && NUM_ITEMS<=256);
    CHECK(0==g_blockDef[B_COBBLE].needTier && 1==g_blockDef[B_LOG].needTier && TOOL_AXE==g_blockDef[B_LOG].tool);
    CHECK(3==g_blockDef[B_IRON_ORE].needTier && 4==g_blockDef[B_DIAMOND_ORE].needTier && 1==g_blockDef[B_COPPER_ORE].needTier);
    CHECK(g_blockDef[B_FIRE].lightEmit==14 && !g_blockDef[B_FIRE_OUT].lightEmit && (g_blockDef[B_ANVIL].flags&BF_USABLE));
    CHECK(g_itemDef[B_FIRE].placeBlock==B_FIRE && !g_itemDef[B_FIRE_OUT].placeBlock);
    CHECK(g_itemDef[I_STONE_PICK].tier==1 && g_itemDef[I_COPPER_PICK].tier==2 && g_itemDef[I_BRONZE_PICK].tier==3);
    CHECK(g_itemDef[I_IRON_PICK].tier==4 && g_itemDef[I_DIAMOND_PICK].tier==5);
    CHECK(TOOL_SPEAR==g_itemDef[I_FLINT_SPEAR].tool && g_itemDef[I_FLINT_SPEAR].durability==131 && g_itemDef[I_BRONZE_SWORD].durability==450);
    {
        int flint=0,tin=0,gravel=0,n;
        for(n=0;n<20000;++n)
        {
            int d=dos_break_drop(B_GRAVEL,B_GRAVEL,B_GRAVEL);
            flint+=d==I_FLINT; tin+=d==I_TIN; gravel+=d==B_GRAVEL;
        }
        CHECK(!tin && flint>1600 && flint<2400 && flint+gravel==20000);
        flint=0;
        for(n=0;n<20000;++n)
        {
            int d=dos_break_drop(B_GRAVEL,MKBLK(B_GRAVEL,GRAVEL_TIN),B_GRAVEL);
            flint+=d==I_FLINT; tin+=d==I_TIN;
        }
        CHECK(tin>120 && tin<300 && flint>1500);
        /* A dropless break (wrong tool) never yields flint or tin. */
        CHECK(0==dos_break_drop(B_GRAVEL,B_GRAVEL,0));
        inv_clear();
        CHECK(I_CLAY_LUMP==dos_break_drop(B_CLAY,B_CLAY,I_CLAY_LUMP) && 3==inv_count(I_CLAY_LUMP));
    }
    printf("PASS: early-game materials\n");
    /* Stations: lists show only their own recipes; grids add the hand's. */
    CHECK(recipe_available(&g_recipes[RC_FIRE_BRICK],ST_FIRE) && !recipe_available(&g_recipes[RC_FIRE_BRICK],ST_FURNACE));
    CHECK(!recipe_available(&g_recipes[RC_PLANKS],ST_FIRE) && recipe_available(&g_recipes[RC_PLANKS],ST_ANVIL));
    CHECK(recipe_available(&g_recipes[RC_STONE_PICK],ST_ANVIL) && !recipe_available(&g_recipes[RC_STONE_PICK],ST_TABLE));
    CHECK(!recipe_available(&g_recipes[RC_IRON_INGOT],ST_FURNACE) && recipe_available(&g_recipes[RC_IRON_INGOT],ST_BLOOMERY));
    CHECK(g_recipes[RC_BRONZE].outCount==11 && g_recipes[RC_BRONZE].in[0][1]==10 && g_recipes[RC_BRONZE].in[1][0]==I_TIN);
    /* Campfire: lit on placing, burns down, refuels, relights with a stick. */
    for(i=0;i<16;++i) zOff[i]=i*16*WH;
    g_zOff=zOff; g_blocks=cells;
    dos_extras_clear(); inv_clear();
    world_set(3,5,4,B_FIRE); dos_block_placed(B_FIRE,3,5,4);
    dos_fire_open(3,5,4);
    CHECK(g_fireLit && recipe_can_craft(&g_recipes[RC_FIRE_CHARCOAL])==0);
    inv_add(B_LOG,1);
    CHECK(recipe_can_craft(&g_recipes[RC_FIRE_CHARCOAL]));
    for(i=0;i<1799;++i) dos_fires_tick();
    CHECK(B_FIRE==wget(3,5,4) && g_fireLit);
    dos_fires_tick();
    CHECK(B_FIRE_OUT==wget(3,5,4) && !g_fireLit && !recipe_can_craft(&g_recipes[RC_FIRE_CHARCOAL]));
    inv_clear(); inv_add(I_STICK,1);
    dos_fire_add_fuel();
    CHECK(B_FIRE_OUT==wget(3,5,4) && inv_count(I_STICK)==1);   /* One stick cannot light and burn */
    inv_add(I_STICK,1);
    dos_fire_add_fuel();
    CHECK(B_FIRE==wget(3,5,4) && !inv_count(I_STICK) && g_fireLit);
    inv_add(B_LOG,10);
    for(i=0;i<10;++i) dos_fire_add_fuel();
    CHECK(inv_count(B_LOG)==7);   /* Full after three logs on top of a stick */
    /* Fires survive a save; breaking one forgets it. */
    count=position=0;
    inv_clear(); chests_clear();
    dos_extras_save(put);
    dos_extras_clear();
    CHECK(dos_extras_load(get) && position==count);
    dos_fire_open(3,5,4);
    CHECK(g_fireLit);
    world_set(3,5,4,B_AIR); dos_block_removed(B_FIRE,3,5,4);
    count=position=0; dos_extras_save(put);
    CHECK(count==2+INV_SLOTS);   /* Magic, wear, end: no fires left */
    printf("PASS: stations and campfires\n");
    /* Spear jab: reach 2 to 4.5 blocks, per-tier cooldown, wear on a hit. */
    inv_clear(); inv_add(I_FLINT_SPEAR,1); g_player.selected=0;
    CHECK(dos_holding_spear());
    rayMob=3; rayDist=FU*3/2; hurts=0;
    CHECK(dos_spear_jab()==15 && hurts==0);
    rayDist=3*FU;
    CHECK(dos_spear_jab()==15 && hurts==1 && hurtMob==3 && hurtDamage==2 && g_inv[0].wear==1);
    rayDist=5*FU;
    CHECK(dos_spear_jab()==15 && hurts==1);
    /* Charge: lowered after the delay, hits a mob ahead closing at 4.6 blocks/s or more. */
    memset(g_mobs,0,sizeof(g_mobs));
    g_mobs[2].type=MOB_ZOMBIE; g_mobs[2].body.z=3*FU;
    g_player.body.x=g_player.body.y=g_player.body.z=0;
    g_player.body.vz=1100; hurts=0; dos_use_held=1;
    for(i=0;i<13;++i) dos_spear_tick();
    CHECK(hurts==0);
    dos_spear_tick();
    CHECK(hurts==1 && hurtMob==2 && hurtDamage==4 && contains(lastMessage,"lowered"));
    g_player.body.vz=800; dos_spear_tick();
    CHECK(hurts==1);                       /* Too slow */
    g_player.body.vz=1100; g_mobs[2].body.x=3*FU; g_mobs[2].body.z=FU; dos_spear_tick();
    CHECK(hurts==1);                       /* Off to the side */
    g_mobs[2].body.x=0; g_mobs[2].body.z=3*FU;
    g_mobs[2].body.vz=-400; g_player.body.vz=600; dos_spear_tick();
    CHECK(hurts==2);                       /* Closing together counts */
    for(i=0;i<70;++i) dos_spear_tick();
    hurts=0; dos_spear_tick();
    CHECK(hurts==0);                       /* The charge has faded */
    dos_use_held=0; dos_spear_tick(); dos_use_held=1;
    for(i=0;i<14;++i) dos_spear_tick();
    CHECK(hurts==1);                       /* Raised and lowered again */
    printf("PASS: spears\n");
    return 0;
}
