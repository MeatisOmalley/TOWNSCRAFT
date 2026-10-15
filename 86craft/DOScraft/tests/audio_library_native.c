#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_bank.h"
#include "audio_voices.h"
int main(int argc,char **argv)
{
    assert(argc==2 && !dos_audio_bank_load(argv[1]));
    assert(dos_audio_bank_count(A_STEP_GRASS)==5 && dos_audio_bank_count(A_GLASS)==3);
    const DosAudioClip *first=dos_audio_bank_clip(A_STEP_GRASS,0),*second=dos_audio_bank_clip(A_STEP_GRASS,1);
    assert(first && second && first->data!=second->data && !dos_audio_bank_clip(A_STEP_GRASS,5));
    unsigned char wave[]={0,128,255,64},out[64];DosAudioClip c={wave,4,11025};
    dos_audio_voices_reset(11025);memset(out,0xee,sizeof(out));
    assert(!dos_audio_voice(&c,256,255,5,0));dos_audio_voices_render(out+8,8);
    assert(out[8]==1 && out[9]==128 && out[10]==254 && out[11]==65);
    for(int i=12;i<16;++i)assert(out[i]==128);
    for(int i=0;i<8;++i)assert(out[i]==0xee && out[16+i]==0xee);
    dos_audio_voices_reset(11025);
    for(int i=0;i<7;++i)assert(!dos_audio_voice(&c,256,255,5,0));
    assert(dos_audio_voice(&c,256,255,1,0)==1 && dos_audio_voice_drops==1);
    dos_audio_voices_render(out,4);assert(out[0]==0 && out[2]==255);
    dos_audio_voices_reset(11025);assert(!dos_audio_voice(&c,1024,255,0,1));
    dos_audio_voices_render(out,8);for(int i=0;i<8;++i)assert(out[i]==1);
    dos_audio_loop_volume(0);dos_audio_voices_render(out,8);for(int i=0;i<8;++i)assert(out[i]==128);
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    unsigned char *data=malloc(n+1);assert(data && fread(data,1,n,f)==(size_t)n);fclose(f);
    assert(!dos_audio_bank_parse(data,n));data[n-1]^=1;
    assert(dos_audio_bank_parse(data,n)<0 && !dos_audio_bank_clip(A_STEP_GRASS,0));data[n-1]^=1;
    assert(!dos_audio_bank_parse(data,n));data[16]=16;assert(dos_audio_bank_parse(data,n)<0);
    assert(dos_audio_bank_load("missing-sound-bank")<0);
    free(data);puts("PASS: external bank, variants, corruption, exact mixer, priority, loops and guards");
    return 0;
}
