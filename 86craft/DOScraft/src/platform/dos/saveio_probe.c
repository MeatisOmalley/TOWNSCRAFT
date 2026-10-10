/* Private HDD diagnostic: actual save transport/codec, mock world/mob data.
 * Not gameplay, real column eviction, or full mob serialization evidence. */
#include "save.c"
#include "system.h"
#include "../../../tests/save_state_fixture.inc"
#include <stdio.h>

int main(void)
{
    int blank=1, legacy=0, banks=0, source=0, paging=0, cleanup, ok;
    u32 elapsed, start;
    u8 bytes[COLUMN_CELLS];
    FILE *report;
    puts("DOScraft persistent save codec diagnostic (not the game)");
    sys_init();
    start=g_ticks;
    if(!fdc_start()) blank=0;
    for(int track=0;blank && track<NUM_TRACKS;++track) {
        if(!fdc_track(track,0)) { blank=0; break; }
        for(int i=0;i<TRACK_BYTES;++i) if(trackBuf[i]) { blank=0; break; }
    }
    if(!dos_save_finish()) blank=0;
    /* Unknown/nonblank media is never reused by this destructive diagnostic. */
    if(blank) {
        fixture_seed(256,1);
        legacy=save_world()==SAVE_OK && fixture_legacy_saves==1;
        fixture_clear_state();
        legacy &= load_world()==SAVE_OK && fixture_restored(256,1) && save_loaded_mobs();
        fixture_seed(32,1);
        banks=save_world()==SAVE_OK && fixture_commits==1;
        fixture_clear_state();
        banks &= load_world()==SAVE_OK && fixture_restored(32,0);
        fixture_seed(32,1); fixture_source=0;
        source=save_world()==SAVE_OK && fixture_commits==2;
        fixture_clear_state();
        source &= load_world()==SAVE_OK && fixture_restored(32,0);
        /* Cross-track reads and adjacent track reuse through the same API. */
        page_cancel();
        paging=save_column_read(BANK_TRACKS*TRACK_BYTES+20+136,COLUMN_CELLS,bytes)==0;
        paging &= save_column_read(BANK_TRACKS*TRACK_BYTES+20+136,COLUMN_CELLS,bytes)==1;
        paging &= ((u32 *)bytes)[0]==4096;
        for(int i=1;i<COLUMN_CELLS/4;++i) if(((u32 *)bytes)[i]!=0x73610000u+i-1) paging=0;
    }
    dos_save_shutdown();
    cleanup=!dos_save_take_close_error() && save_fd<0;
    elapsed=g_ticks-start;
    dos_system_shutdown();
    ok=blank && legacy && banks && source && paging && cleanup;
    report=fopen("C:\\SAVEIO.TXT","wb");
    if(!report) return 1;
    fprintf(report,"DOScraft persistent save transport/codec fixture\n"
        "BLANK_PRIVATE_MEDIUM=%s\nWIDTH256_V3_ROUNDTRIP=%s\nV4_BANK_ROUNDTRIP=%s\n"
        "PROTECTED_SOURCE_BACKUP=%s\nCROSS_TRACK_PAGING=%s\nCLEANUP=%s\nELAPSED_100HZ_TICKS=%u\n"
        "REAL_WORLD_MOB_INTEGRATION=UNTESTED\nRESULT=%s\n",
        blank?"PASS":"FAIL",legacy?"PASS":"FAIL",banks?"PASS":"FAIL",
        source?"PASS":"FAIL",paging?"PASS":"FAIL",cleanup?"PASS":"FAIL",elapsed,ok?"PASS":"FAIL");
    if(fclose(report)) return 1;
    puts(ok?"DOS save codec: PASS":"DOS save codec: FAIL (see C:\\SAVEIO.TXT)");
    return !ok;
}
