/* Two DOS process launches, real world/cache/mobs/inventory and file adapters.
 * Only the deterministic imported terrain and unused diagnostic callbacks are
 * fixtures. No new game policy or codec. Written only on freshly prepared media. */
#include "system.h"
#include "hdd_backend.h"
#include "save_backend.h"
#include "../../../tests/world_storage_fixture.inc"
#include <stdlib.h>

static const char *report_path;
static int started;
static void fixture_cleanup(void)
{
    dos_save_shutdown(); dos_hdd_shutdown();
    if(started) dos_system_shutdown();
    started=0;
}
static void world_storage_check(int ok,const char *message)
{
    if(!ok) {
        fixture_cleanup();
        fprintf(stderr,"WORLDIO failure: %s\n",message);
        FILE *file=report_path ? fopen(report_path,"wb") : NULL;
        if(file) { fprintf(file,"RESULT=FAIL\nREASON=%s\n",message); fclose(file); }
        exit(1);
    }
}
int main(int argc,char **argv)
{
    world_storage_check(argc==2 && (!strcmp(argv[1],"/WRITE") || !strcmp(argv[1],"/RELOAD")),"explicit diagnostic phase");
    int writing=!strcmp(argv[1],"/WRITE");
    report_path=writing ? "C:\\WORLDWR.TXT" : "C:\\WORLDRE.TXT";
    puts(writing ? "DOScraft actual world storage WRITE diagnostic" : "DOScraft fresh-process world storage RELOAD diagnostic");
    sys_init(); started=1;
    u32 start=g_ticks;
    if(writing) fixture_blank_save(DOS_SAVE_PATH);
    world_storage_run(writing);
    u32 elapsed=g_ticks-start;
    fixture_cleanup();
    world_storage_check(hdd_error()==HDD_ERROR_NONE,"terrain cleanup");
    FILE *file=fopen(report_path,"wb");
    world_storage_check(file!=NULL,"create diagnostic report");
    int ok=fprintf(file,"DOScraft actual world/cache/mobs/storage fixture\nPHASE=%s\n"
        "RAM_MB=16\nWIDTH=256\nRESIDENT_COLUMNS=25\nMESH_QUADS_BUDGET=24000\n"
        "REAL_TERRAIN_EVICTION_EDITS=PASS\nTORCH_LIGHT=PASS\nACTIVE_DORMANT_MOBS=PASS\n"
        "V3_SAVE_LOAD=PASS\nPLAYER_INVENTORY_CHEST=PASS\nTERRAIN_READS=%u\nTERRAIN_WRITES=%u\n"
        "TERRAIN_RAM_HITS=%u\nTERRAIN_FAILURES=%u\nELAPSED_100HZ_TICKS=%u\n"
        "SPAWNING_POLICY=UNCHANGED\nGAMEPLAY_AUDIO_INPUT_RENDER_PARITY=UNTESTED\nRESULT=PASS\n",
        writing ? "WRITE" : "FRESH_PROCESS_RELOAD",g_terrainDiskStats[0],g_terrainDiskStats[1],
        g_terrainDiskStats[2],g_terrainDiskStats[3],elapsed)>=0;
    if(fclose(file)) ok=0;
    world_storage_check(ok,"complete diagnostic report");
    puts("DOS real world storage: PASS");
    return 0;
}
