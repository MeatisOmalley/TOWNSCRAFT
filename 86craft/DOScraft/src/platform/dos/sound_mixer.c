#include "sound_mixer.h"
#include "sound_assets.h"
#include <stdint.h>
#include <string.h>

typedef struct {
    const unsigned char *wave;
    uint64_t phase,step,end;
    unsigned int volume,pan_sum;
    int id,active;
} MixerVoice;

static MixerVoice voices[8];
static const unsigned char *wave;
static unsigned int wave_bytes,output_rate,next_voice;

void dos_mixer_stop(void)
{
    memset(voices,0,sizeof(voices));
    next_voice=0;
}

int dos_mixer_init(unsigned int rate)
{
    dos_mixer_stop();
    wave=NULL; wave_bytes=output_rate=0;
    if(!rate) return -1;
    wave=dos_sound_wave(&wave_bytes);
    if(!wave || !wave_bytes) { wave=NULL; wave_bytes=0; return -1; }
    output_rate=rate;
    return 0;
}

static const DosSoundSample *valid_sample(int id,int vol)
{
    const DosSoundSample *sample;
    if(!output_rate || vol<0 || vol>255) return NULL;
    sample=dos_sound_sample(id);
    if(!sample || !sample->frames || !sample->pitch ||
       sample->offset>=wave_bytes || sample->frames>wave_bytes-sample->offset)
        return NULL;
    return sample;
}

static void start_voice(MixerVoice *voice,const DosSoundSample *sample,
                        int id,int pitch,unsigned int volume,unsigned int pan_sum)
{
    uint64_t fd;
    /* Nonpositive products would clamp to 64; avoid signed shifts and signed
     * overflow for all caller ints. Positive division is the source >> 8. */
    if(pitch<=0) fd=64;
    else {
        fd=(uint64_t)sample->pitch*8u*(unsigned int)pitch/256u;
        if(fd<64) fd=64;
        if(fd>65535) fd=65535;
    }
    voice->wave=wave+sample->offset;
    voice->phase=0;
    voice->step=20833u*fd*65536u/(2048u*(uint64_t)output_rate);
    voice->end=(uint64_t)sample->frames*65536u;
    voice->volume=volume; voice->pan_sum=pan_sum;
    voice->id=id; voice->active=1;
}

void dos_mixer_play(int id,int pitch,int vol,int pan)
{
    const DosSoundSample *sample=valid_sample(id,vol);
    unsigned int volume;
    int left,right;
    if(!sample) return;
    volume=(unsigned int)vol*sample->volume/256u;
    if(!volume) return;
    if(pan<-15) pan=-15;
    if(pan>15) pan=15;
    left=15-(pan>0 ? pan : 0);
    right=15-(pan<0 ? -pan : 0);
    start_voice(&voices[next_voice],sample,id,pitch,volume,(unsigned int)(left+right));
    next_voice=(next_voice+1u)%7u;
}

void dos_mixer_loop(int id,int vol)
{
    const DosSoundSample *sample=valid_sample(id,vol);
    MixerVoice *voice=&voices[7];
    unsigned int volume;
    if(!sample) return;
    volume=(unsigned int)vol*sample->volume/256u;
    if(!volume) { voice->active=0; return; }
    if(voice->active && voice->id==id) { voice->volume=volume; return; }
    start_voice(voice,sample,id,256,volume,30);
}

int dos_mixer_render(unsigned char *out,unsigned int frames)
{
    unsigned int frame,ch;
    if(frames>DOS_MIXER_MAX_RENDER_FRAMES || (!out && frames)) return -1;
    for(frame=0;frame<frames;++frame) {
        int sum=0;
        for(ch=0;ch<8;++ch) {
            MixerVoice *voice=&voices[ch];
            int sample;
            uint64_t phase;
            if(!voice->active) continue;
            sample=(int)voice->wave[(unsigned int)(voice->phase/65536u)]-128;
            sum+=sample*(int)voice->volume*(int)voice->pan_sum/(256*30);
            /* With unsigned-int frame lengths, phase < 2^48 and the largest
             * step (rate 1, FD 65535) < 2^36. Their sum cannot overflow 64 bits,
             * even when a step jumps past several loop ends. */
            phase=voice->phase+voice->step;
            if(ch==7) voice->phase=phase%voice->end;
            else if(phase>=voice->end) voice->active=0;
            else voice->phase=phase;
        }
        if(sum<-128) sum=-128;
        if(sum>127) sum=127;
        out[frame]=(unsigned char)(sum+128);
    }
    return 0;
}
