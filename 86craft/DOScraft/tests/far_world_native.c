/* Actual streamed world, save directory and edit summary; no renderer mock. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <io.h>
#include <dpmi.h>
#include "world.c"
#include "../src/platform/dos/heap.h"
#include "hdd_backend.h"
u32 g_ramMB=8;
volatile u32 g_ticks;
static const char *terrain_path;
static u32 directory[3+4096*4],pos;
static void check(int ok,const char *msg) { if(!ok) { fprintf(stderr,"FAIL: %s\n",msg); exit(1); } }
void fatal(const char *msg) { check(0,msg); }
u32 render_mem_needed(int w) { (void)w; return 0; }
int world_test_open(const char *path,int flags,...) {
    check(!strcmp(path,DOS_TERRAIN_PATH) && !(flags&(O_CREAT|O_TRUNC)),"private terrain");
    return open(terrain_path,flags);
}
int __dpmi_int(int vector,__dpmi_regs *r) {
    check(vector==0x21 && r->x.ax==0x6800,"commit only");
    r->x.flags=_commit(r->x.bx) ? 1 : 0; return 0;
}
static void put(u32 v) { check(pos<ARRAY_LEN(directory),"directory bounds"); directory[pos++]=v; }
static u32 get(void) { check(pos<ARRAY_LEN(directory),"directory bounds"); return directory[pos++]; }
int main(int argc,char **argv) {
    check(argc==2,"terrain argument"); terrain_path=argv[1];
    u8 *arena=calloc(4*1048576u,1);
    check(arena && !dos_heap_init(arena,672*1024,arena+672*1024,3072*1024),"1990 arena budget");
    world_alloc(); world_generate(4242);
    check(dos_far_available() && dos_far_map && !dos_far_enabled,"available, default off");
    if(dos_far_compact) {
        int tx=(g_spawnX/CS)*CS+15,tz=(g_spawnZ/CS)*CS+7;
        for(int y=41;y<=45;++y) world_set(tx,y,tz,B_LOG);
        for(int dz=-2;dz<=2;++dz) for(int dx=-2;dx<=2;++dx) world_set(tx+dx,46,tz+dz,B_LEAVES);
        u8 leaf;
        check(dos_far_sample(tx+1,tz,0,&leaf)==47 && leaf==B_LEAVES,"3x3 leaf kept across chunk border");
        check(dos_far_sample(tx-2,tz,0,&leaf)<47 && leaf!=B_LEAVES,"outer leaf omitted from cache");
        check(dos_far_sample(tx-2,tz,1,&leaf)<47 && leaf!=B_LEAVES,"resident boundary does not restore outer leaves");
        check(wget(tx-2,46,tz)==B_LEAVES,"near voxel leaves remain unchanged");
        int rootid=(tz/CS)*g_NC+tx/CS,leafid=(tz/CS)*g_NC+(tx+1)/CS;
        int rootslot=g_columnMap[rootid],leafslot=g_columnMap[leafid],prepared=preparedColumn;
        g_columnMap[rootid]=-1;terrainBlocking=0;
        far_world_column((tx+1)/CS,tz/CS,g_blocks+leafslot*COLUMN_CELLS);
        check(preparedColumn==prepared,"missing streaming neighbor causes no synchronous disk preparation");
        check(dos_far_sample(tx+1,tz,0,&leaf)==47 && leaf==B_LEAVES,"unknown edge retains existing clipped sample");
        g_columnMap[rootid]=rootslot;
        pos=0;check(world_save_large_columns(put),"save clipped leaves and boundary roots");world_commit_large_columns();
        world_set(tx+1,46,tz,B_AIR);
        world_load_position(tx,tz);pos=0;check(world_load_large_columns(get),"reload compact leaves");
        check(dos_far_sample(tx+1,tz,0,&leaf)==47 && leaf==B_LEAVES,"committed boundary canopy reconstructed");
        check(!columnRecords[(tz/CS)*g_NC+tx/CS].raw,"boundary root reconstructed from compressed record");
        for(int y=45;y>=41;--y) world_set(tx,y,tz,B_AIR);
        check(dos_far_sample(tx+1,tz,0,&leaf)<47 && leaf!=B_LEAVES,"cut trunk updates neighboring chunk leaf coverage");
        check(wget(tx+1,46,tz)==B_LEAVES,"distant clipping does not delete real leaves");
        pos=0;check(world_save_large_columns(put),"save cut trunk");world_commit_large_columns();
        world_load_position(tx,tz);pos=0;check(world_load_large_columns(get),"reload cut trunk");
        check(dos_far_sample(tx+1,tz,0,&leaf)<47 && leaf!=B_LEAVES,"cut trunk stays clipped after reload");
        world_load_position(g_spawnX,g_spawnZ);world_generate(4242);
    }
    dos_far_toggle(); check(dos_far_enabled,"optional toggle");
    u8 b=99; check(!dos_far_sample(-1,0,0,&b) && !dos_far_sample(1024,0,0,&b),"world bounds");
    int x=g_spawnX&~1,z=g_spawnZ&~1,y=WH-1;
    world_set(x,y-1,z,B_LOG);u32 version=dos_far_version;
    world_set(x,y,z,B_LEAVES);
    check(dos_far_sample(x,z,0,&b)==WH && b==B_LEAVES,"cached core leaf remains");
    check(dos_far_sample(x,z,1,&b)==WH && b==B_LEAVES,"resident core leaf remains");
    check(dos_far_version>version,"leaf edit invalidates shared distant surface");
    pos=0;check(world_save_large_columns(put),"save log and leaves");world_commit_large_columns();
    world_load_position(x,z);pos=0;check(world_load_large_columns(get),"reload log and leaves");
    check(dos_far_sample(x,z,0,&b)==WH && b==B_LEAVES,"leaf surface survives reload");
    check(wget(x,y,z)==B_LEAVES && BLK_ID(wget(x,y-1,z))==B_LOG,"near-world leaves and logs preserved");
    world_set(x,y,z,B_STONE);
    check(dos_far_sample(x,z,0,&b)==WH && b==B_STONE,"placed highest cube updates summary");
    pos=0; check(world_save_large_columns(put),"save edited directory");
    world_commit_large_columns();
    world_set(x,y,z,B_AIR);
    check(dos_far_sample(x,z,0,&b)<WH,"removal lowers summary");
    world_load_position(x,z);
    pos=0; check(world_load_large_columns(get),"reload committed columns");
    check(dos_far_sample(x,z,0,&b)==WH && b==B_STONE,"saved edit reconstructs distant summary");
    world_set(x,y,z,B_TORCH);
    check(dos_far_sample(x,z,0,&b)<WH,"models do not become distant towers");
    check(heap_high_free()>64*1024,"room for remaining temporary work");
    dos_hdd_shutdown(); dos_heap_shutdown(); free(arena);
    puts("PASS: distant map, bounds, edit/remove/model and committed save reload");
    return 0;
}
