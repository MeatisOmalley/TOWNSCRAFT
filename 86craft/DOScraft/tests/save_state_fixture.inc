/* Codec fixture only: real save byte stream, deterministic mock world/mob
 * providers. Does NOT establish complete world-cache or mob implementation parity. */
int g_W = 32, g_NC = 2, g_spawnX, g_spawnY, g_spawnZ, g_time;
u8 *g_blocks;
Player g_player;
Slot g_inv[INV_SLOTS];
Chest g_chests[MAX_CHESTS];
void (*g_genProgress)(int);
static u8 fixture_blocks[256*256*WH];
static int fixture_cache, fixture_busy, fixture_source = -1;
static int fixture_commits, fixture_imports, fixture_legacy_saves;
static int (*fixture_reader)(u32,u32,u8 *);
static u32 fixture_mobs[8];
static u32 fixture_column_position;
static u8 fixture_source_bytes[COLUMN_CELLS];
int world_cache_active(void) { return fixture_cache; }
int world_backing_idle(void) { return !fixture_busy; }
int world_materialize_columns(void) { return 1; }
void world_set_backing_reader(int (*reader)(u32,u32,u8 *)) { fixture_reader = reader; }
int world_backing_bank(u32 bytes) { (void)bytes; return fixture_source; }
void world_commit_columns(void) { ++fixture_commits; }
void world_import_begin(void) { ++fixture_imports; }
int world_import_end(int ok, int x, int z) { (void)x; (void)z; return ok; }
void world_load_position(int x, int z) { (void)x; (void)z; }
int world_save_columns(void (*put)(u32), u32 (*position)(void))
{
    fixture_column_position = position();
    if(fixture_source>=0) {
        u32 source_offset=fixture_source*(NUM_TRACKS/2*TRACK_BYTES)+20+136;
        int status;
        do { status=fixture_reader(source_offset,COLUMN_CELLS,fixture_source_bytes); } while(!status);
        if(status!=1 || ((u32 *)fixture_source_bytes)[0]!=4096) return 0;
        for(u32 i=1;i<COLUMN_CELLS/4;++i)
            if(((u32 *)fixture_source_bytes)[i]!=0x73610000u+i-1) return 0;
    }
    put(4096);
    for (u32 i=0; i<4096; ++i) put(0x73610000u+i);
    return 1;
}
int world_load_columns(u32 (*get)(void), u32 (*position)(void))
{
    fixture_column_position = position();
    if (get() != 4096) return 0;
    for (u32 i=0; i<4096; ++i) if (get() != 0x73610000u+i) return 0;
    return 1;
}
int world_save_legacy_columns(void (*put)(int))
{
    u32 i=0,n=(u32)g_W*g_W*WH;
    ++fixture_legacy_saves;
    while (i<n) {
        u8 b=g_blocks[i];
        int run=1;
        while(i+(u32)run<n && run<255 && g_blocks[i+run]==b) ++run;
        put(run); put(b); i+=run;
    }
    return 1;
}
void chests_clear(void) { memset(g_chests,0,sizeof(g_chests)); }
void mobs_save(void (*put)(u32)) { for(int i=0;i<8;++i) put(fixture_mobs[i]); }
int mobs_load(u32 (*get)(void)) { for(int i=0;i<8;++i) fixture_mobs[i]=get(); return 1; }
static void fixture_seed(int width, int cache)
{
    g_W=width; g_NC=width/CS; fixture_cache=cache;
    g_blocks=fixture_blocks;
    memset(&g_player,0,sizeof(g_player));
    g_player.body.x=8*FU+3; g_player.body.y=20*FU; g_player.body.z=11*FU+13;
    g_player.yaw=19; g_player.pitch=-21; g_player.health=17; g_player.selected=3;
    g_player.spawnX=5*FU; g_player.spawnY=19*FU; g_player.spawnZ=7*FU;
    g_player.hasBedSpawn=1;
    g_spawnX=2; g_spawnY=22; g_spawnZ=4; g_time=13579;
    for (int i=0; i<INV_SLOTS; ++i) { g_inv[i].item=i; g_inv[i].count=63-i; }
    chests_clear();
    for (int c=0;c<2;++c) {
        g_chests[c].used=1; g_chests[c].x=3+c; g_chests[c].y=12+c; g_chests[c].z=8+c;
        for(int i=0;i<CHEST_SLOTS;++i) { g_chests[c].slot[i].item=i; g_chests[c].slot[i].count=32-i; }
    }
    for (u32 i=0; i<(u32)width*width*WH; ++i) fixture_blocks[i]=(i%1024<100)?B_STONE:B_AIR;
    for (int i=0;i<8;++i) fixture_mobs[i]=0x87654000u+i;
}
static int fixture_restored(int width, int blocks)
{
    if (g_W!=width || g_player.body.x!=8*FU+3 || g_player.body.y!=20*FU ||
        g_player.body.z!=11*FU+13 || g_player.yaw!=19 || g_player.pitch!=-21 ||
        g_player.health!=17 || g_player.selected!=3 || g_player.spawnX!=5*FU ||
        g_player.spawnY!=19*FU || g_player.spawnZ!=7*FU || g_player.hasBedSpawn!=1 ||
        g_spawnX!=2 || g_spawnY!=22 || g_spawnZ!=4 || g_time!=13579) return 0;
    for(int i=0;i<INV_SLOTS;++i)
        if(g_inv[i].item!=i || g_inv[i].count!=63-i) return 0;
    for(int c=0;c<2;++c) {
        if(!g_chests[c].used || g_chests[c].x!=3+c || g_chests[c].y!=12+c || g_chests[c].z!=8+c) return 0;
        for(int i=0;i<CHEST_SLOTS;++i)
            if(g_chests[c].slot[i].item!=i || g_chests[c].slot[i].count!=32-i) return 0;
    }
    for(int i=2;i<MAX_CHESTS;++i) if(g_chests[i].used) return 0;
    for(int i=0;i<8;++i) if(fixture_mobs[i]!=0x87654000u+(u32)i) return 0;
    if(blocks) for(u32 i=0;i<(u32)width*width*WH;++i)
        if(fixture_blocks[i]!=((i%1024<100)?B_STONE:B_AIR)) return 0;
    return 1;
}
static void fixture_clear_state(void)
{
    memset(&g_player,0,sizeof(g_player)); memset(g_inv,0,sizeof(g_inv));
    chests_clear(); memset(fixture_mobs,0,sizeof(fixture_mobs));
    memset(fixture_blocks,0,sizeof(fixture_blocks));
    g_spawnX=g_spawnY=g_spawnZ=g_time=0;
}
