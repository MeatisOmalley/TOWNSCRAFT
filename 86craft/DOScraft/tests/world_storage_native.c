/* Real Win32 file I/O; only fixed DOS path routing and DOS commit are host
 * substitutes. Files are provisioned by Python in a unique private directory.
 * Compile only save.c/hdd.c with -Dopen=world_test_open. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <io.h>
#include <dpmi.h>
#include "heap.h"
#include "hdd_backend.h"
#include "save_backend.h"
#include "world_storage_fixture.inc"

u32 g_ramMB=16;
volatile u32 g_ticks;
static const char *terrain_path,*save_path;
static unsigned int commits;
int world_test_open(const char *path,int flags,...)
{
    const char *actual=NULL;
    if(!strcmp(path,DOS_TERRAIN_PATH)) actual=terrain_path;
    else if(!strcmp(path,DOS_SAVE_PATH)) actual=save_path;
    world_storage_check(actual && !(flags&(O_CREAT|O_TRUNC)),"route only existing private diagnostic media");
    return open(actual,flags);
}
int __dpmi_int(int vector,__dpmi_regs *regs)
{
    world_storage_check(vector==0x21 && regs->x.ax==0x6800,"DOS commit substitute only");
    ++commits;
    regs->x.flags=_commit(regs->x.bx) ? 1 : 0;
    return 0;
}
static void world_storage_check(int ok,const char *message)
{
    if(!ok) { fprintf(stderr,"FAIL world storage: %s\n",message); exit(1); }
}
void fatal(const char *message) { world_storage_check(0,message); }
int main(int argc,char **argv)
{
    unsigned char *arenas;
    world_storage_check(argc==4 && (!strcmp(argv[1],"write") || !strcmp(argv[1],"reload")),"named test and two media paths");
    terrain_path=argv[2]; save_path=argv[3];
    if(!strcmp(argv[1],"write")) fixture_blank_save(save_path);
    arenas=calloc(9*1048576u,1);
    world_storage_check(arenas && !dos_heap_init(arenas,1048576,arenas+1048576,8*1048576),"real bounded DOS arenas");
    world_storage_run(!strcmp(argv[1],"write"));
    dos_save_shutdown(); dos_hdd_shutdown();
    world_storage_check(commits>0 && hdd_error()==HDD_ERROR_NONE,"checked file completion");
    printf("PASS: real world storage %s; HDD reads=%u writes=%u cache=%u commits=%u\n",
        argv[1],g_terrainDiskStats[0],g_terrainDiskStats[1],g_terrainDiskStats[2],commits);
    dos_heap_shutdown(); free(arenas);
    return 0;
}
