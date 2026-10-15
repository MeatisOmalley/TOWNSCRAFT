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
    /* assets/audio/picks.json; glass and pig calls have no picks. */
    assert(dos_audio_bank_count(A_STEP_STONE)==3 && !dos_audio_bank_count(A_GLASS));
    assert(dos_audio_bank_count(A_EXPLODE)==1 && dos_audio_bank_count(A_HISS)==1 && dos_audio_bank_count(A_GROAN)==1);
    assert(dos_audio_bank_count(A_BREAK_GRAVEL)==1 && dos_audio_bank_count(A_CHICKEN_DEATH)==1);
    assert(dos_audio_bank_count(A_WATER_IN)==2 && dos_audio_bank_count(A_SWIM)==2);
    assert(!dos_audio_bank_count(A_PIG_SAY) && !dos_audio_bank_clip(A_PIG_SAY,0));
    const DosAudioClip *first=dos_audio_bank_clip(A_STEP_STONE,0),*second=dos_audio_bank_clip(A_STEP_STONE,1);
    assert(first && second && first->data!=second->data && !dos_audio_bank_clip(A_STEP_STONE,3));
    unsigned char wave[]={0,128,255,64},out[64];DosAudioClip c={wave,4,11025};
    dos_audio_voices_reset(11025);memset(out,0xee,sizeof(out));
    assert(!dos_audio_voice(&c,256,255,5,0));dos_audio_voices_render(out+8,8);
    /* Soft clip: -127 -> -111, 126 -> 111, exact inside the knee. */
    assert(out[8]==17 && out[9]==128 && out[10]==239 && out[11]==65);
    for(int v=-DOS_AUDIO_KNEE;v<=DOS_AUDIO_KNEE;++v)assert(dos_audio_soft_clip(v)==v);
    for(int v=-DOS_AUDIO_MIX_LIMIT;v<DOS_AUDIO_MIX_LIMIT-1;++v)
        assert(dos_audio_soft_clip(v)<=dos_audio_soft_clip(v+1) && dos_audio_soft_clip(v)>=-127 && dos_audio_soft_clip(v)<=127);
    for(int i=12;i<16;++i)assert(out[i]==128);
    for(int i=0;i<8;++i)assert(out[i]==0xee && out[16+i]==0xee);
    dos_audio_voices_reset(11025);
    for(int i=0;i<7;++i)assert(!dos_audio_voice(&c,256,255,5,0));
    assert(dos_audio_voice(&c,256,255,1,0)==1 && dos_audio_voice_drops==1);
    dos_audio_voices_render(out,4);assert(out[0]==3 && out[2]==253);  /* 7 x full scale, compressed */
    dos_audio_voices_reset(11025);assert(!dos_audio_voice(&c,1024,255,0,1));
    dos_audio_voices_render(out,8);for(int i=0;i<8;++i)assert(out[i]==17);
    dos_audio_loop_volume(0);dos_audio_voices_render(out,8);for(int i=0;i<8;++i)assert(out[i]==128);
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    unsigned char *data=malloc(n+1);assert(data && fread(data,1,n,f)==(size_t)n);fclose(f);
    assert(!dos_audio_bank_parse(data,n));data[n-1]^=1;
    assert(dos_audio_bank_parse(data,n)<0 && !dos_audio_bank_clip(A_STEP_GRASS,0));data[n-1]^=1;
    assert(!dos_audio_bank_parse(data,n));data[16]=16;assert(dos_audio_bank_parse(data,n)<0);
    assert(dos_audio_bank_load("missing-sound-bank")<0 && !strcmp(dos_audio_bank_mode(),"NONE"));
    /* Full bank, then a memory limit that forces one variant per group. */
    assert(!dos_audio_bank_load(argv[1]) && !strcmp(dos_audio_bank_mode(),"FULL") && dos_audio_bank_bytes()==(unsigned)n);
    const DosAudioClip *full_first=dos_audio_bank_clip(A_GROAN,0);unsigned int groan_frames=full_first->frames;
    unsigned char *groan_copy=malloc(groan_frames);memcpy(groan_copy,full_first->data,groan_frames);
    unsigned int full_count[A_GROUPS];for(int g=0;g<A_GROUPS;++g)full_count[g]=dos_audio_bank_count(g);
    dos_audio_bank_memory_limit=100u*1024u;
    assert(!dos_audio_bank_load(argv[1]) && !strcmp(dos_audio_bank_mode(),"LITE") && dos_audio_bank_bytes()<100u*1024u);
    for(int g=0;g<A_GROUPS;++g)
        assert(dos_audio_bank_count(g)==(full_count[g]>0) && !dos_audio_bank_clip(g,1) && !dos_audio_bank_clip(g,0)==!full_count[g]);
    assert(dos_audio_bank_clip(A_GROAN,0)->frames==groan_frames && !memcmp(dos_audio_bank_clip(A_GROAN,0)->data,groan_copy,groan_frames));
    /* A corrupt payload byte fails the streamed whole-bank checksum. */
    char bad_path[1024];snprintf(bad_path,sizeof(bad_path),"%s.corrupt",argv[1]);
    FILE *bad=fopen(bad_path,"wb");assert(bad);data[n-1]^=1;fwrite(data,1,n,bad);fclose(bad);data[n-1]^=1;
    assert(dos_audio_bank_load(bad_path)<0 && !strcmp(dos_audio_bank_mode(),"NONE"));remove(bad_path);
    dos_audio_bank_memory_limit=1024;assert(dos_audio_bank_load(argv[1])<0);dos_audio_bank_memory_limit=0;
    free(groan_copy);
    free(data);puts("PASS: external bank, variants, picked and empty groups, corruption, soft-clip mixer, priority, loops and guards");
    return 0;
}
