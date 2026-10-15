/* Direct-port startup assets. Original integer synthesis, score and sorting
 * are generated from verified source bytes, not independently reimplemented.
 * RF5c68 sign-magnitude becomes unsigned mono PCM for the SB software mixer.
 * No downsampling, playback, OPL patch, timer or mixer optimization here.
 * The wave buffer is allocated at its synthesized size after the game arenas;
 * if that fails the score is still built (music without basic effects). */
#include "sound_assets.h"
#include "sound.h"
#include "fmath.h"
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <dpmi.h>
#endif

#define WAVE_LIMIT 65536u
static u8 *wave;
static int counting;
static DosSoundSample samples[NUM_SFX];
static DosSoundEvent events[640];
static int ready,wave_ready,failed;
static void put_byte(u8 b);
#include "towns_audio_assets.inc"

static void put_byte(u8 b)
{
    if(synthTop<0 || (u32)synthTop>=WAVE_LIMIT) { failed=1; return; }
    if(!counting) wave[synthTop]=b;
    ++synthTop;
}

int dos_sound_assets_build(void)
{
    ready=wave_ready=failed=0;
    free(wave); wave=NULL;
    /* Pass 1 sizes the wave and builds the effect table and score. */
    counting=1; synthTop=0; synthSeed=12345; nEvents=songLen=0;
    assets_wave_source(); assets_score_source();
    if(failed || synthTop<=0 || nEvents<=0 || nEvents>MAX_EVENTS ||
        songLen<=0 || songLen>65535) return -1;
    for(int id=0;id<NUM_SFX;++id)
        if((u32)sfx[id].start<<8>=(u32)synthTop || !sfx[id].pitch) return -1;
    for(int i=0;i<nEvents;++i) {
        if(evVoice[i]>1 || evNote[i]>127 || evTick[i]>=songLen ||
            (i && evTick[i]<evTick[i-1])) return -1;
        events[i].tick=evTick[i]; events[i].note=evNote[i]; events[i].voice=evVoice[i];
    }
    ready=1;
    /* Pass 2 writes the same bytes into a buffer of exactly that size. */
    u32 bytes=(u32)synthTop;
    if(!(wave=malloc(bytes))) return 1;
    /* Alignment gaps were never played by Towns; define them as silence in
     * this DOS buffer, rather than expose uninitialized RAM. */
    memset(wave,0,bytes);
    counting=0; synthTop=0; synthSeed=12345;
    assets_wave_source();
    if(failed || (u32)synthTop!=bytes) { free(wave); wave=NULL; ready=0; return -1; }
    for(int id=0;id<NUM_SFX;++id) {
        u32 start=(u32)sfx[id].start<<8,end=start;
        while(end<bytes && wave[end]!=0xff) ++end;
        if(end==bytes || end==start) { free(wave); wave=NULL; ready=0; return -1; }
        samples[id].offset=start; samples[id].frames=end-start;
        samples[id].pitch=sfx[id].pitch; samples[id].volume=sfx[id].vol;
    }
    for(u32 i=0;i<bytes;++i) {
        int b=wave[i];
        wave[i]=b==255 ? 128 : ((b&128) ? 128-(b&127) : 128+b);
    }
#ifdef __DJGPP__
    /* The SB IRQ mixes from this buffer. */
    _go32_dpmi_lock_data(wave,bytes);
#endif
    wave_ready=1;
    return 0;
}

const unsigned char *dos_sound_wave(unsigned int *bytes)
{
    if(bytes) *bytes=wave_ready ? (u32)synthTop : 0;
    return wave_ready ? wave : NULL;
}
const DosSoundSample *dos_sound_sample(int id)
{
    return wave_ready && id>=0 && id<NUM_SFX ? &samples[id] : NULL;
}
int dos_sound_level(int id)
{
    return ready && id>=0 && id<NUM_SFX ? sfx[id].vol : 0;
}
const DosSoundEvent *dos_sound_events(unsigned int *count,unsigned int *song_ticks)
{
    if(count) *count=ready ? (u32)nEvents : 0;
    if(song_ticks) *song_ticks=ready ? (u32)songLen : 0;
    return ready ? events : NULL;
}
