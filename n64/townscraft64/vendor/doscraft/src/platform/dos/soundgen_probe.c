/* Audio DATA diagnostic only: no SB/OPL playback or audible parity claim.
 * Write an explicit little-endian diagnostic wire for independent host/source
 * comparison. It is not an asset/save format consumed by the game. */
#include "sys.h"
#include "system.h"
#include "sound.h"
#include "sound_assets.h"
#include <stdio.h>

static int word(FILE *file,unsigned int value)
{
    unsigned char bytes[4]={value,value>>8,value>>16,value>>24};
    return fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes);
}
static int write_assets(unsigned int bytes,unsigned int count,unsigned int ticks)
{
    FILE *file=fopen("C:\\SNDASSET.BIN","wb");
    if(!file) return 0;
    int ok=word(file,0x41534344) && word(file,1) && word(file,bytes) && word(file,count) && word(file,ticks);
    for(int id=0;id<NUM_SFX && ok;++id) {
        const DosSoundSample *s=dos_sound_sample(id);
        ok=s && word(file,s->offset) && word(file,s->frames) && word(file,s->pitch) && word(file,s->volume);
    }
    const unsigned char *wave=dos_sound_wave(NULL);
    if(ok) ok=fwrite(wave,1,bytes,file)==bytes;
    const DosSoundEvent *events=dos_sound_events(NULL,NULL);
    for(unsigned int i=0;i<count && ok;++i) {
        unsigned char record[4]={events[i].tick,events[i].tick>>8,events[i].note,events[i].voice};
        ok=fwrite(record,1,sizeof(record),file)==sizeof(record);
    }
    if(fclose(file)) ok=0;
    return ok;
}
int main(void)
{
    unsigned int bytes=0,count=0,ticks=0;
    puts("DOScraft retained audio synthesis/score diagnostic (no playback)");
    sys_init();
    u32 start=g_ticks;
    int ok=dos_sound_assets_build()==0;
    if(ok) {
        ok=dos_sound_wave(&bytes)!=NULL && dos_sound_events(&count,&ticks)!=NULL &&
            bytes<=65536 && count<=640 && dos_sound_sample(-1)==NULL && dos_sound_sample(NUM_SFX)==NULL;
    }
    if(ok) ok=write_assets(bytes,count,ticks);
    u32 elapsed=g_ticks-start;
    dos_system_shutdown();
    FILE *report=fopen("C:\\SOUNDGEN.TXT","wb");
    if(!report) return 1;
    int written=fprintf(report,"DOScraft retained audio data diagnostic\nWAVE_BYTES=%u\nEFFECTS=%u\n"
        "SCORE_EVENTS=%u\nSONG_TICKS=%u\nELAPSED_100HZ_TICKS=%u\n"
        "SB_OPL_PLAYBACK=UNIMPLEMENTED\nAUDIBLE_PARITY=UNTESTED\nRESULT=%s\n",
        bytes,(unsigned int)NUM_SFX,count,ticks,elapsed,ok ? "PASS" : "FAIL")>=0;
    if(fclose(report)) written=0;
    puts(ok && written ? "DOS retained audio data: PASS" : "DOS retained audio data: FAIL");
    return !(ok && written);
}
