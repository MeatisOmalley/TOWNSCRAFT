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
    dos_far_toggle(); check(dos_far_enabled,"optional toggle");
    u8 b=99; check(!dos_far_sample(-1,0,0,&b) && !dos_far_sample(1024,0,0,&b),"world bounds");
    int x=g_spawnX&~1,z=g_spawnZ&~1,y=WH-1;
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
