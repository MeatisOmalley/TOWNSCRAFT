#include <stdio.h>
#include "early_game.h"
#include "player.h"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

Player g_player;
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
    return 0;
}
