#include "audio_bank.h"
#include <stdio.h>
#include <string.h>
#define CAPACITY (192u*1024u)
static unsigned char storage[CAPACITY];
static DosAudioClip clips[64];
static unsigned char groups[64];
static unsigned int count;
static unsigned int word(const unsigned char *p)
{ return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static unsigned int sum(const unsigned char *p,unsigned int n)
{ unsigned int h=2166136261u;while(n--) h=(h^*p++)*16777619u;return h; }
int dos_audio_bank_parse(const unsigned char *data,unsigned int bytes)
{
    count=0;
    if(!data || bytes<32 || bytes>CAPACITY || memcmp(data,"DCSFX01\0",8) || word(data+8)!=1) return -1;
    unsigned int rate=word(data+12),format=word(data+16),n=word(data+20),length=word(data+24);
    if(rate<6000 || rate>44100 || format!=(8u|(1u<<16)) || !n || n>64 ||
       bytes<32+n*16 || length!=bytes-32-n*16) return -1;
    const unsigned char *payload=data+32+n*16;
    if(sum(payload,length)!=word(data+28)) return -1;
    unsigned int end=0;
    for(unsigned int i=0;i<n;++i) {
        const unsigned char *p=data+32+i*16;
        unsigned int group=word(p),offset=word(p+4),frames=word(p+8);
        if(group>=A_GROUPS || offset!=end || !frames || frames>32767 ||
           offset>length || frames>length-offset || sum(payload+offset,frames)!=word(p+12)) return -1;
        groups[i]=(unsigned char)group;
        clips[i]=(DosAudioClip){payload+offset,frames,rate};end=offset+frames;
    }
    if(end!=length) return -1;
    count=n;return 0;
}
int dos_audio_bank_load(const char *path)
{
    count=0;FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned int n=fread(storage,1,sizeof(storage),f);
    int extra=fgetc(f),failed=ferror(f);fclose(f);
    return extra!=EOF || failed ? -1 : dos_audio_bank_parse(storage,n);
}
unsigned int dos_audio_bank_count(int group)
{ unsigned int n=0;for(unsigned int i=0;i<count;++i)n+=groups[i]==group;return n; }
const DosAudioClip *dos_audio_bank_clip(int group,unsigned int variant)
{
    for(unsigned int i=0;i<count;++i)if(groups[i]==group && !variant--)return &clips[i];
    return 0;
}
