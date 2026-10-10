/* Direct-port startup assets. Original integer synthesis, score and sorting
 * are generated from verified source bytes, not independently reimplemented.
 * RF5c68 sign-magnitude becomes unsigned mono PCM for the SB software mixer.
 * No downsampling, playback, OPL patch, timer or mixer optimization here. */
#include "sound_assets.h"
#include "sound.h"
#include "fmath.h"

static u8 wave[65536];
static DosSoundSample samples[NUM_SFX];
static DosSoundEvent events[640];
static int ready,failed;
static void put_byte(u8 b);
#include "towns_audio_assets.inc"

static void put_byte(u8 b)
{
    if(synthTop<0 || (u32)synthTop>=sizeof(wave)) { failed=1; return; }
    wave[synthTop++]=b;
}

int dos_sound_assets_build(void)
{
    ready=failed=0;
    synthTop=0; synthSeed=12345; nEvents=songLen=0;
    /* Alignment gaps were never played by Towns; define them as silence in
     * this borrowed DOS buffer, rather than expose uninitialized RAM. */
    memset(wave,0,sizeof(wave));
    assets_wave_source(); assets_score_source();
    if(failed || synthTop<=0 || nEvents<=0 || nEvents>MAX_EVENTS ||
        songLen<=0 || songLen>65535) return -1;
    for(int id=0;id<NUM_SFX;++id) {
        u32 start=(u32)sfx[id].start<<8,end=start;
        if(start>=(u32)synthTop || !sfx[id].pitch) return -1;
        while(end<(u32)synthTop && wave[end]!=0xff) ++end;
        if(end==(u32)synthTop || end==start) return -1;
        samples[id].offset=start; samples[id].frames=end-start;
        samples[id].pitch=sfx[id].pitch; samples[id].volume=sfx[id].vol;
    }
    for(int i=0;i<nEvents;++i) {
        if(evVoice[i]>1 || evNote[i]>127 || evTick[i]>=songLen ||
            (i && evTick[i]<evTick[i-1])) return -1;
        events[i].tick=evTick[i]; events[i].note=evNote[i]; events[i].voice=evVoice[i];
    }
    for(int i=0;i<synthTop;++i) {
        int b=wave[i];
        wave[i]=b==255 ? 128 : ((b&128) ? 128-(b&127) : 128+b);
    }
    ready=1;
    return 0;
}

const unsigned char *dos_sound_wave(unsigned int *bytes)
{
    if(bytes) *bytes=ready ? (u32)synthTop : 0;
    return ready ? wave : NULL;
}
const DosSoundSample *dos_sound_sample(int id)
{
    return ready && id>=0 && id<NUM_SFX ? &samples[id] : NULL;
}
const DosSoundEvent *dos_sound_events(unsigned int *count,unsigned int *song_ticks)
{
    if(count) *count=ready ? (u32)nEvents : 0;
    if(song_ticks) *song_ticks=ready ? (u32)songLen : 0;
    return ready ? events : NULL;
}
