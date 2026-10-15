#include "audio_voices.h"
#include <stdint.h>
#include <string.h>
typedef struct { const unsigned char *data;unsigned int phase,step,end,gain,priority,active; } Voice;
static Voice voices[8];
static unsigned int rate,cursor;
/* Soft clip: exact up to +-DOS_AUDIO_KNEE, then a knee that approaches +-127,
 * so overlapping loud effects compress instead of hard clipping. */
static unsigned char clip_table[2*DOS_AUDIO_MIX_LIMIT];
unsigned int dos_audio_voice_drops,dos_audio_mixed_frames;
int dos_audio_soft_clip(int value)
{
    int magnitude=value<0 ? -value : value;
    if(magnitude>DOS_AUDIO_KNEE) {
        int over=magnitude-DOS_AUDIO_KNEE,room=127-DOS_AUDIO_KNEE;
        magnitude=DOS_AUDIO_KNEE+room*over/(over+room);
    }
    return value<0 ? -magnitude : magnitude;
}
void dos_audio_voices_stop(void) { memset(voices,0,sizeof(voices));cursor=0; }
void dos_audio_voices_reset(unsigned int hz)
{
    dos_audio_voices_stop();rate=hz>=6000 && hz<=44100 ? hz : 0;dos_audio_voice_drops=dos_audio_mixed_frames=0;
    for(int v=-DOS_AUDIO_MIX_LIMIT;v<DOS_AUDIO_MIX_LIMIT;++v)
        clip_table[v+DOS_AUDIO_MIX_LIMIT]=(unsigned char)(dos_audio_soft_clip(v)+128);
}
int dos_audio_voice(const DosAudioClip *clip,int pitch,int volume,int priority,int loop)
{
    if(!rate || !clip || !clip->data || !clip->frames || clip->frames>32767 ||
       clip->rate<1000 || clip->rate>65535 || volume<=0 || volume>255)return -1;
    if(pitch<64)pitch=64;
    if(pitch>1024)pitch=1024;
    if(priority<0)priority=0;
    if(priority>255)priority=255;
    unsigned int step=(unsigned int)((uint64_t)clip->rate*256u*(unsigned)pitch/rate);
    if(!step)step=1;
    int slot=7;
    if(!loop) {
        slot=-1;
        for(unsigned int i=0;i<7;++i) {
            unsigned int s=(cursor+i)%7;
            if(!voices[s].active) { slot=s;break; }
            if(voices[s].priority<=(unsigned)priority &&
               (slot<0 || voices[s].priority<voices[slot].priority))slot=s;
        }
        if(slot<0) { ++dos_audio_voice_drops;return 1; }
        cursor=((unsigned)slot+1)%7;
    }
    voices[slot]=(Voice){clip->data,0,step,clip->frames<<16,(unsigned)volume,(unsigned)priority,1};
    return 0;
}
void dos_audio_loop_volume(int volume)
{
    if(volume<0)volume=0;
    if(volume>255)volume=255;
    voices[7].gain=(unsigned)volume;
    if(!volume)voices[7].active=0;
}
void dos_audio_voices_render(unsigned char *out,unsigned int frames)
{
    Voice *active[8];unsigned int n=0;
    for(unsigned int ch=0;ch<8;++ch)if(voices[ch].active)active[n++]=&voices[ch];
    for(unsigned int i=0;i<frames;++i) {
        if(!n) { memset(out+i,128,frames-i);break; }
        int value=0;
        for(unsigned int ch=0;ch<n;) {
            Voice *v=active[ch];
            value+=(((int)v->data[v->phase>>16]-128)*(int)v->gain)/256;
            unsigned int phase=v->phase+v->step;
            if(phase>=v->end) {
                if(v==&voices[7])phase%=v->end;
                else { v->active=0;active[ch]=active[--n];continue; }
            }
            v->phase=phase;
            ++ch;
        }
        if(value<-DOS_AUDIO_MIX_LIMIT)value=-DOS_AUDIO_MIX_LIMIT;
        if(value>DOS_AUDIO_MIX_LIMIT-1)value=DOS_AUDIO_MIX_LIMIT-1;
        out[i]=clip_table[value+DOS_AUDIO_MIX_LIMIT];
    }
    dos_audio_mixed_frames+=frames;
}
