/* Actual world/cache implementation with private host file routing only. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <io.h>
#include <dpmi.h>
#include "world.c"
#include "hdd_backend.h"
#include "../src/platform/dos/heap.h"

u32 g_ramMB=8;
volatile u32 g_ticks;
static const char *terrain_path;
static void check(int ok,const char *message)
{ if(!ok) { fprintf(stderr,"FAIL: %s\n",message); exit(1); } }
void fatal(const char *message) { check(0,message); }
u32 render_mem_needed(int w) { (void)w; return 0; }
int world_test_open(const char *path,int flags,...)
{
    check(!strcmp(path,DOS_TERRAIN_PATH) && !(flags&(O_CREAT|O_TRUNC)),"route existing private terrain only");
    return open(terrain_path,flags);
}
int __dpmi_int(int vector,__dpmi_regs *regs)
{
    check(vector==0x21 && regs->x.ax==0x6800,"DOS commit substitute only");
    regs->x.flags=_commit(regs->x.bx) ? 1 : 0; return 0;
}
int main(int argc,char **argv)
{
    check(argc==2,"private prebaked medium path"); terrain_path=argv[1];
    u8 *arena=calloc(9*1048576u,1);
    check(arena && !dos_heap_init(arena,1048576,arena+1048576,8*1048576),"bounded arenas");
    world_alloc(); world_generate(4242);
    check(g_W==1024 && streamLightBaked,"baked 1024 seed restored");
    /* An inner column has every light-influencing neighbor resident. The
     * untouched bake must match the pinned runtime flood-fill byte for byte. */
    int cx=g_spawnX/CS,cz=g_spawnZ/CS,slot=g_columnMap[cz*g_NC+cx];
    u8 expected[COLUMN_CELLS],height[CS*CS];
    memcpy(expected,g_light+slot*COLUMN_CELLS,COLUMN_CELLS);
    memcpy(height,g_height+slot*CS*CS,CS*CS);
    light_init();
    check(!memcmp(expected,g_light+slot*COLUMN_CELLS,COLUMN_CELLS),"baked light equals canonical flood fill");
    check(!memcmp(height,g_height+slot*CS*CS,CS*CS),"baked height equals canonical height");
    int tx=g_spawnX+40,tz=g_spawnZ;
    for(int n=0;n<1000;++n) cache_stream(tx,tz,1);
    check(!streamError && !cache_light_pending(),"polled streaming completes without host input");
    slot=g_columnMap[(tz/CS)*g_NC+tx/CS]; check(slot>=0,"polled destination resident");
    memcpy(expected,g_light+slot*COLUMN_CELLS,COLUMN_CELLS);
    light_init();
    check(!memcmp(expected,g_light+slot*COLUMN_CELLS,COLUMN_CELLS),"polled clean restore equals canonical light");
    check(world_verify_stream_light(),"seam torch/remove/roof/reopen after cold eviction");
    check(g_streamLightStats[0] && g_streamLightStats[1] && g_streamLightStats[3],"baked/RAM restore and invalidation exercised");
    printf("PASS: canonical baked light; cold eviction, seam and edit invalidation (%u,%u,%u,%u)\n",
        g_streamLightStats[0],g_streamLightStats[1],g_streamLightStats[2],g_streamLightStats[3]);
    dos_hdd_shutdown(); dos_heap_shutdown(); free(arena); return 0;
}
