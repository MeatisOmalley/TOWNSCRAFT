/* V3 edit log through the actual world cache, save codec and DOS file
 * adapters. Only DOS path routing and the DOS commit call are host
 * substitutes; every scenario runs as a separate process so a reload never
 * relies on RAM from the run that wrote the media.
 *
 *   new   T S           new world (both save banks cleared)
 *   edit  T S c0 n v k r  mark n columns from path index c0 with value v,
 *                       saving after every k columns (k=0: no save; the last
 *                       records stay unsaved, as after a crash before the
 *                       bank commit); r rounds, value v+round
 *   check T S c0 n v    load, then every marked column must hold value v
 *   fill  T S c0 v      mark columns from c0 with polled play-time eviction
 *                       until the log refuses a record; then save
 *   fail  T S           load must fail (corrupt directory or spawn record)
 *   probe T S c0 n v    load; columns are checked one at a time and a column
 *                       that fails to stream reports CORRUPT=<id> instead
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <io.h>
#include <time.h>
#include <dpmi.h>
#include "world.c"
#include "mobs.c"
#include "save.h"
#include "heap.h"
#include "hdd_backend.h"
#include "save_backend.h"

u32 g_ramMB=8;
volatile u32 g_ticks;
Player g_player;
int g_time;
static const char *terrain_path,*save_path;
static void check(int ok,const char *message)
{ if(!ok) { fprintf(stderr,"FAIL edit log: %s\n",message); exit(1); } }
void fatal(const char *message) { check(0,message); }
int world_test_open(const char *path,int flags,...)
{
    const char *actual=!strcmp(path,DOS_TERRAIN_PATH) ? terrain_path :
                       !strcmp(path,DOS_SAVE_PATH) ? save_path : NULL;
    check(actual && !(flags&(O_CREAT|O_TRUNC)),"route only existing private media");
    return open(actual,flags);
}
int __dpmi_int(int vector,__dpmi_regs *regs)
{
    check(vector==0x21 && regs->x.ax==0x6800,"DOS commit substitute only");
    regs->x.flags=_commit(regs->x.bx) ? 1 : 0;
    return 0;
}
u32 render_mem_needed(int w) { (void)w; return 0; }
void sound_play_at(int sfx,int pitch,int vol,int x,int y,int z)
{ (void)sfx; (void)pitch; (void)vol; (void)x; (void)y; (void)z; }
void player_hurt(int dmg,int fromX,int fromZ) { (void)dmg; (void)fromX; (void)fromZ; check(0,"combat"); }
void body_tick(Body *b,int gravity) { (void)b; (void)gravity; check(0,"physics"); }
int face_light_level(int light,int dir,int darken) { (void)light; (void)dir; (void)darken; return 0; }
MBox *render_add_box(void) { check(0,"renderer"); return NULL; }
void game_message(const char *message) { (void)message; }

/* Column id's marker cell; values cycle through solid blocks. */
static const u8 marks[]={B_COBBLE,B_PLANKS,B_WOOL,B_STONEBRICK,B_GLASS,B_GRAVEL};
static void marker(int id,int *x,int *z) { *x=id%g_NC*CS+id%CS; *z=id/g_NC*CS+id/CS%CS; }
/* Serpentine order keeps each step one column from the last. */
static int path_id(int i) { int cz=i/64,cx=i%64; return cz*64+(cz&1 ? 63-cx : cx); }

static void visit(int id)
{
    cacheBoot=1; cache_stream(id%g_NC*CS+8,id/g_NC*CS+8,100000); cacheBoot=0;
    check(!streamError && world_column_ready(id%g_NC,id/g_NC),"column streamed");
}
/* Ordinary play-time travel: one bounded step per frame, no reserve. */
static int visit_polled(int id)
{
    int x=id%g_NC*CS+8,z=id/g_NC*CS+8;
    for(int step=0; step<20000; ++step) {
        cache_stream(x,z,1);
        if(streamError) return 0;
        if(world_column_ready(id%g_NC,id/g_NC) && cacheLightSlot<0) return 1;
    }
    check(0,"bounded polled travel");
    return 0;
}
static void place_player(int id)
{
    int x=id%g_NC*CS+8,z=id/g_NC*CS+8;
    g_player.body.x=x*FU; g_player.body.z=z*FU; g_player.body.y=(WH-2)*FU;
    g_player.health=20;
}
static void save_now(void)
{
    clock_t t0=clock();
    check(save_world()==SAVE_OK,"save commits");
    printf("SAVE ms=%ld\n",(long)((clock()-t0)*1000/CLOCKS_PER_SEC));
}
static void start(int argc,char **argv,int fresh)
{
    check(argc>=4,"mode, terrain and save paths");
    terrain_path=argv[2]; save_path=argv[3];
    u8 *arenas=calloc(9*1048576u,1);
    check(arenas && !dos_heap_init(arenas,1048576,arenas+1048576,8*1048576),"bounded DOS arenas");
    world_alloc();
    check(g_W==1024 && terrainLarge && terrainV2,"compact 1024 medium");
    mobs_clear(); inv_clear(); chests_clear();
    if(fresh) {
        check(dos_save_clear_world()==SAVE_OK,"clear both banks");
        world_generate(4242);
        memset(&g_player,0,sizeof(g_player));
        place_player(g_spawnZ/CS*g_NC+g_spawnX/CS);
    } else {
        int status=load_world();
        if(strcmp(argv[1],"fail")) check(status==SAVE_OK,"load committed save");
        else {
            check(status!=SAVE_OK,"damaged save or record is refused");
            puts("PASS: refused"); exit(0);
        }
        world_rebuild_after_load();
    }
}
static void log_report(void)
{
    u32 used=0,records=0;
    if(terrainV3) {
        log_rebuild();
        for(int id=0; id<TERRAIN_COLUMNS; ++id) {
            u32 ref=large_ref(&columnRecords[id]);
            if(ref) { ++records; used+=ref&31u; }
        }
    }
    printf("LOG sectors=%u free=%u live_records=%u live_sectors=%u slots=%u\n",
        terrainV3 ? terrainLogEnd-terrainLogFirst : 0,terrainV3 ? logFree : 0,records,used,
        terrainV3 ? log_slots(~0u) : 0);
    printf("STATS rebuilds=%u wraps=%u compactions=%u moved=%u moved_sectors=%u refused=%u writes=%u failed=%u\n",
        g_terrainLogStats[0],g_terrainLogStats[1],g_terrainLogStats[2],g_terrainLogStats[3],
        g_terrainLogStats[4],g_terrainLogStats[6],g_terrainDiskStats[1],g_terrainDiskStats[3]);
}
static void finish(void)
{
    log_report();
    check(world_backing_idle() && !g_terrainDiskStats[3],"terrain settles without failed commands");
    dos_save_shutdown(); dos_hdd_shutdown();
    puts("PASS");
}

/* Run-boundary scans against a bit-by-bit reference on random bitmaps. */
static void bitmap_scans(void)
{
    u32 seed=12345;
    for(int trial=0; trial<400; ++trial) {
        u32 size=1+(trial*997u)%LOG_MAX_SECTORS,used=trial%7;
        terrainLogFirst=5000; terrainLogEnd=5000+size;
        memset(logBits,0,sizeof(logBits));
        for(u32 i=0; i<size; ) {
            seed=seed*1103515245u+12345u;
            u32 run=1+(seed>>16)%(trial%3 ? 40 : 4);
            if(((seed>>8)&7)<used) for(u32 k=i; k<i+run && k<size; ++k) logBits[k>>5]|=1u<<(k&31);
            i+=run;
        }
        u32 slots=0,run=0;
        for(u32 i=0; i<=size; ++i) {
            if(i<size && !log_bit(i)) ++run;
            else { slots+=run/TERRAIN_SECTORS; run=0; }
        }
        check(log_slots(~0u)==slots && (slots<3 ? log_slots(3)==slots : log_slots(3)>=3),"slot count");
        for(int q=0; q<20; ++q) {
            seed=seed*1103515245u+12345u;
            u32 from=(seed>>4)%size,end=from+(seed>>12)%(size-from+1),n=1+(seed>>20)%24;
            u32 s0=(seed>>3)%size,s1=s0+(seed>>9)%64,want=~0u;
            if(q&1) s0=s1=0;
            for(u32 i=from,r=0; i<end; ++i) {
                if(log_bit(i) || (i>=s0 && i<s1)) { r=0; continue; }
                if(++r==n) { want=i+1-n; break; }
            }
            check(log_find(from,end,n,s0,s1)==want,"free-run search");
        }
    }
    puts("PASS: bitmap scans");
}

int main(int argc,char **argv)
{
    check(argc>=2,"mode");
    const char *mode=argv[1];
    if(!strcmp(mode,"bits")) { bitmap_scans(); return 0; }
    if(!strcmp(mode,"new")) { start(argc,argv,1); place_player(g_spawnZ/CS*g_NC+g_spawnX/CS); save_now(); finish(); return 0; }
    if(!strcmp(mode,"fail")) { start(argc,argv,0); return 1; }
    check(argc>=7,"column range and value");
    int c0=atoi(argv[4]),n=atoi(argv[5]),v=atoi(argv[6]);
    check(c0>=0 && n>=0 && c0+n<=4096 && v>=0 && v<(int)sizeof(marks),"column range");
    if(!strcmp(mode,"edit")) {
        int every=argc>7 ? atoi(argv[7]) : 0,rounds=argc>8 ? atoi(argv[8]) : 1;
        start(argc,argv,0);
        for(int round=0; round<rounds; ++round) {
            int value=(v+round)%(int)sizeof(marks);
            for(int i=c0; i<c0+n; ++i) {
                int id=path_id(i),x,z;
                visit(id); marker(id,&x,&z);
                world_set(x,WH-1,z,marks[value]);
                check(wget(x,WH-1,z)==marks[value],"marker placed");
                if(every && (i-c0+1)%every==0) { place_player(id); save_now(); }
            }
        }
        /* Unsaved: write every dirty column to the log without a commit. */
        if(!every) check(world_store_flush(),"records written");
        else if(n%every || !n) { place_player(path_id(n ? c0+n-1 : c0)); save_now(); }
        finish(); return 0;
    }
    if(!strcmp(mode,"fill")) {
        start(argc,argv,0);
        int done=0,refused=0;
        for(int i=c0; i<4096 && !refused; ++i) {
            int id=path_id(i),x,z;
            if(!visit_polled(id)) { refused=1; break; }
            marker(id,&x,&z); world_set(x,WH-1,z,marks[v]); ++done;
            if(streamError==2) refused=1;
        }
        check(refused && g_terrainLogStats[6]>0 && streamError==2,"play-time records are refused when full");
        printf("FILL columns=%d\n",done);
        streamError=0;
        place_player(path_id(c0+done-1));
        save_now(); /* The flush may use the reserve. */
        finish(); return 0;
    }
    if(!strcmp(mode,"check") || !strcmp(mode,"probe")) {
        start(argc,argv,0);
        int bad=0;
        for(int i=c0; i<c0+n; ++i) {
            int id=path_id(i),x,z;
            if(!strcmp(mode,"probe")) {
                cacheBoot=1; cache_stream(id%g_NC*CS+8,id/g_NC*CS+8,100000); cacheBoot=0;
                /* A damaged neighbor may stop the stream after the target. */
                if(!world_column_ready(id%g_NC,id/g_NC)) {
                    check(streamError==1,"corruption reports a read failure");
                    printf("CORRUPT=%d\n",id); ++bad; streamError=0; continue;
                }
                streamError=0;
            } else visit(id);
            marker(id,&x,&z);
            check(wget(x,WH-1,z)==marks[v],"saved marker survives reload");
        }
        printf("CHECKED=%d BAD=%d\n",n,bad);
        finish(); return 0;
    }
    check(0,"unknown mode");
    return 1;
}
