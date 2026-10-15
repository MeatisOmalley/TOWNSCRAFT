#include "audio_bank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <dpmi.h>
#endif
#define CAPACITY (192u*1024u)
#define MAX_CLIPS 64u
/* Allocated at sound_init, after the game arenas. A 4 MiB 386 has little left,
 * so when the whole bank does not fit, one variant of each group loads. */
static unsigned char *storage;
static DosAudioClip clips[MAX_CLIPS];
static unsigned char groups[MAX_CLIPS];
static unsigned int count,loaded_bytes,lite;
unsigned int dos_audio_bank_memory_limit;
static unsigned int word(const unsigned char *p)
{ return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static unsigned int sum_more(unsigned int h,const unsigned char *p,unsigned int n)
{ while(n--) h=(h^*p++)*16777619u;return h; }
static unsigned int sum(const unsigned char *p,unsigned int n) { return sum_more(2166136261u,p,n); }
/* Header and contiguous-descriptor checks; data holds at least 32+n*16 bytes
 * of a bank that is bytes long. Returns the clip count, or 0. */
static unsigned int header(const unsigned char *data,unsigned int bytes,unsigned int *rate,unsigned int *length)
{
    if(bytes<32 || bytes>CAPACITY || memcmp(data,"DCSFX01\0",8) || word(data+8)!=1) return 0;
    unsigned int n=word(data+20);
    *rate=word(data+12);*length=word(data+24);
    if(*rate<6000 || *rate>44100 || word(data+16)!=(8u|(1u<<16)) || !n || n>MAX_CLIPS ||
       bytes<32+n*16 || *length!=bytes-32-n*16) return 0;
    unsigned int end=0;
    for(unsigned int i=0;i<n;++i) {
        const unsigned char *p=data+32+i*16;
        unsigned int group=word(p),offset=word(p+4),frames=word(p+8);
        if(group>=A_GROUPS || offset!=end || !frames || frames>32767 || frames>*length-offset) return 0;
        end=offset+frames;
    }
    return end==*length ? n : 0;
}
int dos_audio_bank_parse(const unsigned char *data,unsigned int bytes)
{
    count=lite=0;
    unsigned int rate,length,n=data ? header(data,bytes,&rate,&length) : 0;
    if(!n) return -1;
    const unsigned char *payload=data+32+n*16;
    if(sum(payload,length)!=word(data+28)) return -1;
    for(unsigned int i=0;i<n;++i) {
        const unsigned char *p=data+32+i*16;
        unsigned int offset=word(p+4),frames=word(p+8);
        if(sum(payload+offset,frames)!=word(p+12)) return -1;
        groups[i]=(unsigned char)word(p);
        clips[i]=(DosAudioClip){payload+offset,frames,rate};
    }
    count=n;return 0;
}
static void release(void) { free(storage);storage=0;count=lite=loaded_bytes=0; }
static void *allocate(unsigned int bytes)
{ return dos_audio_bank_memory_limit && bytes>dos_audio_bank_memory_limit ? 0 : malloc(bytes); }
/* Stream the payload once: verify its checksum and copy the first variant of
 * every group (clips are contiguous and in offset order). */
static int load_lite(FILE *f,unsigned int size)
{
    static unsigned char head[32+MAX_CLIPS*16];
    unsigned int rate,length,n,keep[A_GROUPS],base[A_GROUPS],kept=0,want=0;
    unsigned char seen[A_GROUPS]={0};
    if(fseek(f,0,SEEK_SET) || fread(head,1,32,f)!=32) return -1;
    n=word(head+20);
    if(!n || n>MAX_CLIPS || fread(head+32,1,n*16,f)!=n*16 || !(n=header(head,size,&rate,&length))) return -1;
    for(unsigned int i=0;i<n;++i) {
        unsigned int group=word(head+32+i*16);
        if(!seen[group]) { seen[group]=1;keep[kept]=i;base[kept++]=want;want+=word(head+32+i*16+8); }
    }
    if(!(storage=allocate(want))) return -1;
    unsigned char chunk[512];unsigned int whole=2166136261u,at=0,next=0;
    while(at<length) {
        unsigned int part=length-at<sizeof(chunk) ? length-at : (unsigned)sizeof(chunk);
        if(fread(chunk,1,part,f)!=part) return -1;
        whole=sum_more(whole,chunk,part);
        for(unsigned int j=next;j<kept;++j) {
            const unsigned char *p=head+32+keep[j]*16;
            unsigned int offset=word(p+4),end=offset+word(p+8);
            if(offset>=at+part) break;
            unsigned int from=offset>at ? offset : at,to=end<at+part ? end : at+part;
            if(to>from) memcpy(storage+base[j]+(from-offset),chunk+(from-at),to-from);
            if(end<=at+part) next=j+1;
        }
        at+=part;
    }
    if(whole!=word(head+28)) return -1;
    for(unsigned int j=0;j<kept;++j) {
        const unsigned char *p=head+32+keep[j]*16;
        unsigned int frames=word(p+8);
        if(sum(storage+base[j],frames)!=word(p+12)) return -1;
        groups[j]=(unsigned char)word(p);
        clips[j]=(DosAudioClip){storage+base[j],frames,rate};
    }
    count=kept;lite=1;loaded_bytes=want;
    return 0;
}
static int load(FILE *f)
{
    if(fseek(f,0,SEEK_END))return -1;
    long size=ftell(f);
    if(size<32 || size>(long)CAPACITY || fseek(f,0,SEEK_SET))return -1;
    if((storage=allocate((unsigned)size))) {
        if(fread(storage,1,(size_t)size,f)!=(size_t)size || dos_audio_bank_parse(storage,(unsigned)size))return -1;
        loaded_bytes=(unsigned)size;
        return 0;
    }
    return load_lite(f,(unsigned)size);
}
int dos_audio_bank_load(const char *path)
{
    release();
    FILE *f=fopen(path,"rb");if(!f)return -1;
    int result=load(f);
    fclose(f);
    if(result)release();
#ifdef __DJGPP__
    else _go32_dpmi_lock_data(storage,loaded_bytes);    /* The SB IRQ mixes from it */
#endif
    return result;
}
const char *dos_audio_bank_mode(void) { return !count ? "NONE" : lite ? "LITE" : "FULL"; }
unsigned int dos_audio_bank_bytes(void) { return loaded_bytes; }
unsigned int dos_audio_bank_count(int group)
{ unsigned int n=0;for(unsigned int i=0;i<count;++i)n+=groups[i]==group;return n; }
const DosAudioClip *dos_audio_bank_clip(int group,unsigned int variant)
{
    for(unsigned int i=0;i<count;++i)if(groups[i]==group && !variant--)return &clips[i];
    return 0;
}
