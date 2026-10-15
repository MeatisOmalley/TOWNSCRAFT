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
int dos_cpu386;
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
    FILE *seed=fopen(terrain_path,"rb");u32 header[24];check(seed && fread(header,4,24,seed)==24,"seed header");fclose(seed);
    if(header[23]==DOS_FAR_LEAF_FLAGS_MAGIC) {
        int tx=-1,tz=0,th=0,rx=-1,rz=0,rh=0;
        for(int slot=0;slot<g_residentColumns;++slot) {
            if(!residentReady[slot]) continue;
            int cx=g_columnCoords[slot*2],cz=g_columnCoords[slot*2+1];
            for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
                const u8 *p=g_blocks+slot*COLUMN_CELLS+(z*16+x)*WH;int h=g_height[slot*256+z*16+x];
                int bx=cx*16+x,bz=cz*16+z,at=((bz>>1)*512+(bx>>1))*2;
                if(h && p[h-1]==(B_LEAVES|DOS_LEAF_RIM) && BLK_ID(dos_far_map[at+1])!=B_LEAVES && rx<0) { rx=bx;rz=bz;rh=h; }
                if(BLK_ID(dos_far_map[at+1])==B_LEAVES && tx<0 && p[dos_far_map[at]-1]==B_LEAVES) { tx=bx;tz=bz;th=dos_far_map[at]; }
            }
        }
        check(tx>=0 && rx>=0,"included and excluded baked leaves are resident");u8 leaf;
        check(dos_far_sample(rx,rz,1,&leaf)<rh && leaf!=B_LEAVES,"resident rim leaves are skipped by flag");
        u32 version=dos_far_version;world_set(rx,rh-1,rz,B_AIR);
        check(dos_far_version==version,"destroying an excluded rim leaf does not change the surface");
        check(dos_far_sample(tx,tz,0,&leaf)==th && leaf==B_LEAVES,"included baked leaf visible");
        world_set(tx,WH-1,tz,B_STONE);world_set(tx,WH-1,tz,B_AIR);
        check(dos_far_sample(tx,tz,0,&leaf)==th && leaf==B_LEAVES,"cover removal retains baked selection");
        int rootx=-1,rootz=0;
        for(int dz=-1;dz<=1 && rootx<0;++dz) for(int dx=-1;dx<=1 && rootx<0;++dx)
            for(int y=th-1;y>=MAX(0,th-8);--y) if(BLK_ID(wget(tx+dx,y,tz+dz))==B_LOG) { rootx=tx+dx;rootz=tz+dz;break; }
        check(rootx>=0,"included canopy has a natural trunk");
        for(int y=WH-1;y>=0;--y) if(BLK_ID(wget(rootx,y,rootz))==B_LOG) world_set(rootx,y,rootz,B_AIR);
        check(dos_far_sample(tx,tz,0,&leaf)==th && leaf==B_LEAVES,"cutting trunk does not hide surviving leaves");
        world_set(tx,th-1,tz,B_AIR);
        pos=0;check(world_save_large_columns(put),"save destroyed leaves and trunk");world_commit_large_columns();
        world_load_position(tx,tz);pos=0;check(world_load_large_columns(get,6),"reload fixed eligibility");
        check(wget(tx,th-1,tz)==B_AIR && wget(rx,rh-1,rz)==B_AIR,"leaf destructions survive reload");
        int cell=((tz>>1)*512+(tx>>1))*2;u8 h=0,b=0;
        for(int dz=0;dz<2;++dz) for(int dx=0;dx<2;++dx) {
            int x=(tx&~1)+dx,z=(tz&~1)+dz;
            for(int y=WH-1;y>=h;--y) if(dos_far_cube(wget(x,y,z))) { h=y+1;b=BLK_ID(wget(x,y,z));break; }
        }
        check(dos_far_map[cell]==h && BLK_ID(dos_far_map[cell+1])==b,"reload selects surviving included leaves and cubes");
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
    world_load_position(x,z);pos=0;check(world_load_large_columns(get,6),"reload log and leaves");
    check(dos_far_sample(x,z,0,&b)==WH && b==B_LEAVES,"leaf surface survives reload");
    check(wget(x,y,z)==B_LEAVES && BLK_ID(wget(x,y-1,z))==B_LOG,"near-world leaves and logs preserved");
    world_set(x,y,z,B_STONE);
    check(dos_far_sample(x,z,0,&b)==WH && b==B_STONE,"placed highest cube updates summary");
    pos=0; check(world_save_large_columns(put),"save edited directory");
    world_commit_large_columns();
    world_set(x,y,z,B_AIR);
    check(dos_far_sample(x,z,0,&b)<WH,"removal lowers summary");
    world_load_position(x,z);
    pos=0; check(world_load_large_columns(get,6),"reload committed columns");
    check(dos_far_sample(x,z,0,&b)==WH && b==B_STONE,"saved edit reconstructs distant summary");
    world_set(x,y,z,B_TORCH);
    check(dos_far_sample(x,z,0,&b)<WH,"models do not become distant towers");
    check(heap_high_free()>64*1024,"room for remaining temporary work");
    dos_hdd_shutdown(); dos_heap_shutdown(); free(arena);
    puts("PASS: distant map, bounds, edit/remove/model and committed save reload");
    return 0;
}
