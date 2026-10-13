#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "mobs.c"

int g_W=96,g_NC=6;
u32 g_ramMB=8;
Player g_player;
u8 *g_blocks,*g_light,*g_columnReady,*g_height;
int *g_zOff;
short *g_columnMap,*g_columnCoords;
int g_residentColumns,g_allocChunkCount;
static MBox rendered[160];
static int nRendered,allocations;
static u32 wire[10000];
static int count,position;

void *heap_alloc_low(u32 bytes) { ++allocations; void *p=calloc(1,bytes); assert(p); return p; }
void sound_play_at(int id,int pitch,int vol,int x,int y,int z)
{ (void)id;(void)pitch;(void)vol;(void)x;(void)y;(void)z; }
void world_set(int x,int y,int z,u8 b) { assert(in_world(x,y,z));g_blocks[widx(x,y,z)]=b; }
void player_hurt(int dmg,int x,int z) { (void)dmg;(void)x;(void)z;assert(!"Chicken attacked"); }
void game_message(const char *message) { assert(message); }
int world_surface_y(int x,int z) { (void)x;(void)z;return 1; }
int world_light_at(int x,int y,int z) { (void)x;(void)y;(void)z;return 0xf0; }
int face_light_level(int light,int dir,int dark) { (void)light;(void)dir;(void)dark;return 15; }
MBox *render_add_box(void) { assert(nRendered<160);return &rendered[nRendered++]; }
static void put(u32 value) { assert(count<10000);wire[count++]=value; }
static u32 get(void) { assert(position<count);return wire[position++]; }
static void reset(void)
{
    mobs_clear();memset(regionVisited,1,sizeof(regionVisited));inv_clear();
    memset(&g_player,0,sizeof(g_player));g_player.body.x=g_player.body.z=48*FU;
    g_player.body.y=FU;g_player.health=20;
}
int main(void)
{
    assert(MOB_TNT==5 && MOB_CHICKEN==6 && I_RAW_PORK==45 && I_GOLDEN_APPLE==58);
    assert(I_RAW_CHICKEN==59 && I_FEATHER==61);
    g_blocks=calloc(g_W*g_W,WH);g_zOff=calloc(g_W,sizeof(int));assert(g_blocks && g_zOff);
    for(int z=0;z<g_W;++z) {
        g_zOff[z]=z*g_W*WH;
        for(int x=0;x<g_W;++x) g_blocks[widx(x,0,z)]=B_GRASS;
    }
    items_init();reset();
    int chicken=mob_spawn(MOB_CHICKEN,48*FU,FU,50*FU);assert(chicken>=0);
    Mob *m=&g_mobs[chicken];assert(m->health==4 && m->body.hw==819 && m->body.h==2867);
    assert(!is_hostile(m->type) && mobs_count(0)==1 && mobs_count(1)==0);
    int distance;assert(mobs_ray_hit(48*FU,FU+FU/3,47*FU,0,0,16384,5*FU,&distance)==chicken);
    assert(distance>2*FU && distance<4*FU);
    m->walking=1;m->aiTimer=200;m->yaw=m->targetYaw=0;m->body.onGround=1;
    int before=m->body.z;for(int i=0;i<10;++i) mobs_tick(0);
    assert(m->body.z>before && m->body.y==FU && m->health==4 && g_player.health==20);
    nRendered=0;mobs_add_render_boxes(0);assert(nRendered==7);
    for(int i=0;i<nRendered;++i) for(int face=0;face<6;++face)
        assert(rendered[i].tex[face]>=T_CHICKEN_SKIN && rendered[i].tex[face]<=T_CHICKEN_YELLOW);
    m->body.y=8*FU;m->body.vy=-6000;m->body.onGround=0;m->walking=0;
    before=m->body.y;mobs_tick(0);assert(before-m->body.y<=500 && m->body.vy>=-500);
    nRendered=0;mobs_add_render_boxes(0);assert(rendered[5].pitch!=0 && rendered[6].pitch!=0);
    /* Legacy mob IDs and the new chicken survive the same unchanged wire codec. */
    m->body.y=FU;m->body.vy=0;m->body.onGround=1;
    int pig=mob_spawn(MOB_PIG,49*FU,FU,49*FU);assert(pig>=0);
    u32 chickenID=g_mobIDs[chicken],pigID=g_mobIDs[pig];
    count=position=0;mobs_save(put);assert(mobs_load(get));assert(position==count);
    assert(g_mobIDs[0]==chickenID && g_mobIDs[1]==pigID && g_mobs[1].type==MOB_PIG);
    assert(g_mobs[0].type==MOB_CHICKEN && g_mobs[0].body.hw==819 && g_mobs[0].health==4);
    wire[5+g_NC*g_NC+1]=NUM_MOB_TYPES;position=0;assert(!mobs_load(get));
    reset();chicken=mob_spawn(MOB_CHICKEN,48*FU,FU,50*FU);
    mob_hurt(chicken,4,48*FU,48*FU,1);assert(!g_mobs[chicken].type);
    assert(inv_count(I_RAW_CHICKEN)==1 && inv_count(I_FEATHER)>=0 && inv_count(I_FEATHER)<=2);
    const Recipe *cooking=NULL;
    for(int i=0;i<g_numRecipes;++i) if(g_recipes[i].out==I_COOKED_CHICKEN) cooking=&g_recipes[i];
    assert(cooking && cooking->station==ST_FURNACE && !recipe_can_craft(cooking));
    inv_add(I_COAL,1);assert(recipe_craft(cooking));
    assert(inv_count(I_RAW_CHICKEN)==0 && inv_count(I_COOKED_CHICKEN)==1);
    assert(g_itemDef[I_RAW_CHICKEN].food==2 && g_itemDef[I_COOKED_CHICKEN].food==6);
    reset();chicken=mob_spawn(MOB_CHICKEN,88*FU,FU,48*FU);
    mobs_regions_tick();assert(!g_mobs[chicken].type && mobs_dormant_count()==0);
    int seen=0;
    for(int seed=1;seed<=32;++seed) {
        mobs_clear();rnd_seed(seed);mobs_spawn_initial(seed);
        for(int i=0;i<MAX_MOBS;++i) if(g_mobs[i].type==MOB_CHICKEN) {
            ++seen;assert(wget(g_mobs[i].body.x/FU,0,g_mobs[i].body.z/FU)==B_GRASS);
        }
        assert(mobs_count(0)<=passive_limit());
    }
    assert(seen>0 && allocations==1);
    /* Worst-case mixed passive/hostile/TNT population fits existing box cap. */
    assert((MAX_MOBS-hostile_limit()-2)*7+hostile_limit()*6+2<=160);
    puts("PASS: chicken spawn, AI, physics, model, combat, drops, cooking, save IDs and despawn");
    return 0;
}
