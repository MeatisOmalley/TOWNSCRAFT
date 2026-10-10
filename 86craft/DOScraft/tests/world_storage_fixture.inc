/* Integration diagnostic, not game code. Include the unchanged staged world
 * and mob manager to inspect private cache ownership, as Towns tests do.
 * Link real inventory, blocks/math/RNG and DOS heap/save/terrain adapters.
 * A deterministic imported flat world isolates transport from generation.
 * Unused audio/render/combat/physics callbacks fail if accidentally invoked.
 * No AI ticks, input, rendering or gameplay parity are claimed by this test. */
#include "world.c"
#include "mobs.c"
#include "save.h"
#include <stdio.h>

Player g_player;
int g_time;
static void world_storage_check(int ok,const char *message);

static void fixture_blank_save(const char *path)
{
    unsigned char bytes[4096];
    unsigned int total=0;
    FILE *file=fopen(path,"rb");
    world_storage_check(file!=NULL,"private save file exists");
    size_t n;
    while((n=fread(bytes,1,sizeof(bytes),file))!=0) {
        total+=n;
        world_storage_check(total<=154*8192u,"private save extent bounded");
        for(size_t i=0;i<n;++i) world_storage_check(!bytes[i],"write test refuses nonblank save");
    }
    int ok=!ferror(file) && total==154*8192u;
    if(fclose(file)) ok=0;
    world_storage_check(ok,"blank private save has exact extent");
}

void sound_play_at(int sfx,int pitch,int vol,int x,int y,int z)
{ (void)sfx; (void)pitch; (void)vol; (void)x; (void)y; (void)z; world_storage_check(0,"unexpected audio"); }
void player_hurt(int dmg,int fromX,int fromZ)
{ (void)dmg; (void)fromX; (void)fromZ; world_storage_check(0,"unexpected combat"); }
void body_tick(Body *b,int gravity)
{ (void)b; (void)gravity; world_storage_check(0,"unexpected physics tick"); }
int face_light_level(int light,int dir,int darken)
{ (void)light; (void)dir; (void)darken; world_storage_check(0,"unexpected renderer lighting"); return 0; }
MBox *render_add_box(void) { world_storage_check(0,"unexpected renderer"); return NULL; }
void game_message(const char *message)
{ (void)message; world_storage_check(0,"unexpected game message"); }

static u8 fixture_block(int x,int y,int z)
{
    const u8 markers[]={B_STONE,B_COBBLE,B_LOG,B_WOOL};
    if(x==49 && y==40 && z==49) return B_PLANKS;
    if(x==50 && y==40 && z==49) return B_TORCH;
    if(y==0) return B_BEDROCK;
    if(y==1) return B_GRASS;
    if(y==2 && x%CS==3 && z%CS==3) return markers[(x/CS+3*(z/CS))%4];
    return B_AIR;
}
static void fixture_resident(int edited)
{
    for(int slot=0;slot<g_residentColumns;++slot) {
        int cx=g_columnCoords[slot*2],cz=g_columnCoords[slot*2+1];
        if(cx<0) continue;
        world_storage_check(g_columnMap[cz*g_NC+cx]==slot,"unique resident owner");
        for(int lz=0;lz<CS;++lz) for(int lx=0;lx<CS;++lx) for(int y=0;y<WH;++y) {
            int x=cx*CS+lx,z=cz*CS+lz;
            u8 expected=fixture_block(x,y,z);
            if(!edited && y==40 && z==49 && (x==49 || x==50)) expected=B_AIR;
            world_storage_check(wget(x,y,z)==expected,"exact resident terrain");
        }
    }
}
static void fixture_travel(int x,int z)
{
    int ready=0;
    g_player.body.x=x*FU; g_player.body.z=z*FU;
    for(int step=0;step<2000 && !ready;++step) {
        mobs_regions_tick();
        world_update_dirty_chunks(1);
        world_stream(x,z,20,1);
        save_stream_tick();
        world_storage_check(!world_stream_error(),"cooperative travel transport");
        ready=cacheLightSlot<0;
        int pcx=x/CS,pcz=z/CS;
        if(cacheDirectionX>0 && (x&15)>=12) pcx=MIN(pcx+1,g_NC-1);
        if(cacheDirectionX<0 && (x&15)<=3) pcx=MAX(pcx-1,0);
        if(cacheDirectionZ>0 && (z&15)>=12) pcz=MIN(pcz+1,g_NC-1);
        if(cacheDirectionZ<0 && (z&15)<=3) pcz=MAX(pcz-1,0);
        for(int cz=MAX(0,pcz-2);cz<=MIN(g_NC-1,pcz+2);++cz)
        for(int cx=MAX(0,pcx-2);cx<=MIN(g_NC-1,pcx+2);++cx)
            if(!world_column_sim_ready(cx,cz)) ready=0;
    }
    world_storage_check(ready,"bounded cooperative travel settles");
    mobs_regions_tick();
    world_storage_check(g_residentColumns==25 && g_allocChunkCount==78,"unchanged resident budgets");
}
static void fixture_mobs(void)
{
    int near=-1;
    for(int i=0;i<MAX_MOBS;++i) if(g_mobs[i].type) {
        world_storage_check(near<0 && g_mobIDs[i]==2,"only expected active identity"); near=i;
    }
    world_storage_check(near>=0 && g_mobs[near].type==MOB_SHEEP && g_mobs[near].health==5 &&
        g_mobs[near].aiTimer==61 && g_mobs[near].body.x==48*FU && g_mobs[near].body.z==48*FU,
        "active sheep state");
    world_storage_check(dormantCount==1 && dormant[0].id==1 && dormant[0].mob.type==MOB_PIG &&
        dormant[0].mob.health==6 && dormant[0].mob.panic==17 && dormant[0].mob.body.vx==123 &&
        dormant[0].mob.body.x==200*FU && dormant[0].mob.body.z==200*FU && nextMobID==3,
        "frozen pig state and IDs");
    world_storage_check(regionSeed==4242,"mob population seed");
    for(int i=0;i<g_NC*g_NC;++i) world_storage_check(regionVisited[i]==1,"visited empty regions persist");
}
static void fixture_state(void)
{
    fixture_mobs(); fixture_resident(1);
    world_storage_check(g_player.body.x==48*FU && g_player.body.y==2*FU && g_player.body.z==48*FU &&
        g_player.health==19 && g_player.yaw==123 && g_player.pitch==-77 && g_player.selected==2 &&
        g_player.spawnX==48*FU && g_player.spawnY==2*FU && g_player.spawnZ==48*FU &&
        g_player.hasBedSpawn==1 && g_time==12345,"player and clock state");
    world_storage_check(g_spawnX==48 && g_spawnY==2 && g_spawnZ==48,"world spawn state");
    world_storage_check(g_inv[2].item==B_LOG && g_inv[2].count==9,"inventory state");
    Chest *chest=chest_at(51,2,49,0);
    world_storage_check(chest && chest->slot[3].item==B_STONE && chest->slot[3].count==7,"chest state");
    world_storage_check((world_light_at(51,40,49)&15)==13,"torch relight after load/eviction");
}
static void fixture_import(void)
{
    world_import_begin();
    for(int z=0;z<g_W;++z) for(int x=0;x<g_W;++x) for(int y=0;y<WH;++y) {
        u8 b=fixture_block(x,y,z);
        if(y==40 && z==49 && (x==49 || x==50)) b=B_AIR;
        g_blocks[widx(x,y,z)]=b;
    }
    g_spawnX=g_spawnZ=48; g_spawnY=2;
    world_storage_check(world_import_end(1,48,48),"flat fixture archived through real HDD");
    world_storage_check(!world_stream_error() && world_hdd_active() && world_store_used()<columnCapacity,
        "HDD archive and bounded materialization capacity");
    for(int id=0;id<g_NC*g_NC;++id) world_storage_check(columnRecords[id].disk==2,"all initial records on HDD");
    fixture_resident(0);
}
static void world_storage_run(int writing)
{
    world_alloc();
    world_storage_check(g_ramMB==16 && g_W==256 && g_NC==16 && poolSize==24000 && world_hdd_active(),
        "primary RAM, extent, mesh and terrain backend unchanged");
    mobs_clear(); inv_clear(); chests_clear();
    world_set_column_pin(mobs_column_pinned); world_set_column_needed(mobs_column_needed);
    if(writing) {
        fixture_import();
        memset(&g_player,0,sizeof(g_player));
        g_player.body.x=g_player.body.z=48*FU; g_player.body.y=2*FU;
        g_player.spawnX=g_player.spawnZ=48*FU; g_player.spawnY=2*FU;
        g_player.health=19; g_player.yaw=123; g_player.pitch=-77; g_player.selected=2; g_player.hasBedSpawn=1;
        g_time=12345; g_inv[2].item=B_LOG; g_inv[2].count=9;
        Chest *chest=chest_at(51,2,49,1);
        world_storage_check(chest!=NULL,"create chest fixture");
        chest->slot[3].item=B_STONE; chest->slot[3].count=7;
        regionSeed=4242; memset(regionVisited,1,sizeof(regionVisited));
        int pig=mob_spawn(MOB_PIG,200*FU,2*FU,200*FU);
        world_storage_check(pig>=0,"create distant pig");
        g_mobs[pig].health=6; g_mobs[pig].panic=17; g_mobs[pig].body.vx=123;
        g_mobs[pig].yaw=g_mobs[pig].targetYaw=21;
        mobs_regions_tick();
        int sheep=mob_spawn(MOB_SHEEP,48*FU,2*FU,48*FU);
        world_storage_check(sheep>=0,"create nearby sheep");
        g_mobs[sheep].health=5; g_mobs[sheep].aiTimer=61;
        g_mobs[sheep].yaw=g_mobs[sheep].targetYaw=58;
        world_set(49,40,49,B_PLANKS); world_set(50,40,49,B_TORCH);
        world_update_dirty_chunks(0);
        world_storage_check((world_light_at(51,40,49)&15)==13,"initial torch light");
        u32 writes=g_terrainDiskStats[1];
        fixture_travel(208,208);
        world_storage_check(!world_column_ready(3,3) && g_terrainDiskStats[1]>writes,"dirty column evicts to HDD");
        fixture_resident(1);
        fixture_travel(48,48); fixture_state();
        world_storage_check(g_terrainDiskStats[0]>0 && g_terrainDiskStats[2]>0 && !g_terrainDiskStats[3],"HDD reads and existing RAM cache");
        world_storage_check(save_world()==SAVE_OK,"actual width256 v3 save");
        for(int id=0;id<g_NC*g_NC;++id) world_storage_check(!columnRecords[id].disk,"v3 materializes original records");
        world_set(49,40,49,B_AIR); mobs_clear(); inv_clear(); chests_clear(); g_time=0;
    }
    world_storage_check(load_world()==SAVE_OK && save_loaded_mobs(),"actual v3 terrain/mobs load");
    world_rebuild_after_load(); fixture_state();
    fixture_travel(208,208); fixture_resident(1);
    fixture_travel(48,48); fixture_state();
    world_storage_check(world_backing_idle() && !g_terrainDiskStats[3],"all terrain operations settle");
}
