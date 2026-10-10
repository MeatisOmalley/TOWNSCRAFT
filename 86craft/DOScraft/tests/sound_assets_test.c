/* Public DOS asset API parity against separately compiled, full Towns sound.c.
   No playback, device driver, timer, resampling or synthesis replacement. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound_assets.h"
#include "sound.h"

void towns_reference_build(void);
const unsigned char *towns_reference_raw_wave(unsigned int *bytes);
void towns_reference_sample(int id,unsigned int fields[4]);
void towns_reference_score(unsigned int *count,unsigned int *ticks);
void towns_reference_event(unsigned int id,unsigned int fields[3]);

enum { WAVE_CAPACITY=65536, EVENT_CAPACITY=640,
       WIRE_CAPACITY=20+10*16+WAVE_CAPACITY+EVENT_CAPACITY*4 };
typedef char ten_samples[(NUM_SFX==10) ? 1 : -1];
typedef char word_width[(sizeof(unsigned int)==4) ? 1 : -1];
static const char *caseName="arguments";
static unsigned char wire[WIRE_CAPACITY],snapshot[WIRE_CAPACITY];
static unsigned int wireSize;
static void check(int ok,const char *message)
{
    if(!ok) { fprintf(stderr,"FAIL audio assets %s: %s\n",caseName,message); exit(1); }
}
static unsigned char unsigned_sample(unsigned char raw)
{
    if(raw==0xFF) return 128;
    /* Independent sign/magnitude interpretation; includes negative zero. */
    int magnitude=raw&0x7F;
    int signedValue=(raw&0x80) ? -magnitude : magnitude;
    return (unsigned char)(signedValue+128);
}
static void before_build(void)
{
    unsigned int bytes=UINT_MAX,count=UINT_MAX,ticks=UINT_MAX;
    check(!dos_sound_wave(&bytes) && !bytes,"before build wave NULL and zero bytes");
    check(!dos_sound_events(&count,&ticks) && !count && !ticks,
          "before build events NULL and zero lengths");
    check(!dos_sound_wave(NULL) && !dos_sound_events(NULL,NULL),"before build optional output pointers");
    for(int id=0;id<NUM_SFX;++id) check(!dos_sound_sample(id),"before build sample NULL");
}
static void invalid_ids(void)
{
    const int invalid[]={INT_MIN,-1024,-1,NUM_SFX,NUM_SFX+1,1024,INT_MAX};
    for(int pass=0;pass<2;++pass)
    {
        for(unsigned int i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
            check(!dos_sound_sample(invalid[i]),"invalid sample ID returns NULL");
        if(!pass) check(!dos_sound_assets_build(),"build after invalid ID queries");
    }
    for(int id=0;id<NUM_SFX;++id) check(dos_sound_sample(id)!=NULL,"every valid sample ID available");
    check(dos_sound_wave(NULL)!=NULL && dos_sound_events(NULL,NULL)!=NULL,
          "built getters permit NULL output pointers");
    unsigned int count=0,ticks=0;
    check(dos_sound_events(&count,NULL)!=NULL && count>0,"events count-only getter");
    check(dos_sound_events(NULL,&ticks)!=NULL && ticks>0,"events ticks-only getter");
}
static void build_both(void)
{
    towns_reference_build();
    check(dos_sound_assets_build()==0,"DOS assets build succeeds");
}
static void parity(void)
{
    unsigned int rawBytes=0,bytes=0,count=0,ticks=0,refCount=0,refTicks=0;
    const unsigned char *raw=towns_reference_raw_wave(&rawBytes);
    const unsigned char *wave=dos_sound_wave(&bytes);
    const DosSoundEvent *events=dos_sound_events(&count,&ticks);
    check(raw && wave && events && bytes>0 && bytes<=WAVE_CAPACITY && bytes==rawBytes,
          "exact bounded wave length");
    towns_reference_score(&refCount,&refTicks);
    check(count==refCount && ticks==refTicks && count>0 && count<=EVENT_CAPACITY,
          "exact merged event count and song length");
    unsigned int positive=0,negative=0,markers=0;
    for(unsigned int i=0;i<bytes;++i)
    {
        if(raw[i]==255) ++markers;
        else if(raw[i]&128) ++negative;
        else if(raw[i]) ++positive;
        check(wave[i]==unsigned_sample(raw[i]),"every generated unsigned sample byte");
    }
    check(positive && negative && markers==NUM_SFX+1,"reference signs and silence/effect markers");
    unsigned int previousEnd=3;
    for(int id=0;id<NUM_SFX;++id)
    {
        unsigned int expected[4]; towns_reference_sample(id,expected);
        const DosSoundSample *sample=dos_sound_sample(id);
        check(sample && sample->offset==expected[0] && sample->frames==expected[1] &&
              sample->pitch==expected[2] && sample->volume==expected[3],
              "exact sample offset, frames, base pitch and volume");
        check(!(sample->offset&255) && sample->offset>=previousEnd &&
              sample->frames>0 && sample->offset+sample->frames<bytes,"descriptor extent and alignment");
        for(unsigned int i=previousEnd;i<sample->offset;++i)
            check(raw[i]==0 && wave[i]==128,"alignment gaps convert to unsigned silence");
        check(wave[sample->offset+sample->frames]==128,"effect marker converts to silence outside frames");
        previousEnd=sample->offset+sample->frames+1;
    }
    check(previousEnd==bytes && wave[0]==128 && wave[1]==128 && wave[2]==128,
          "exact final marker and page-zero silence loop");
    unsigned int voices[2]={0,0},ties=0;
    for(unsigned int i=0;i<count;++i)
    {
        unsigned int expected[3]; towns_reference_event(i,expected);
        check(events[i].tick==expected[0] && events[i].note==expected[1] &&
              events[i].voice==expected[2],"every merged score tick, note and voice");
        check(events[i].voice<2 && events[i].note<=127 && events[i].tick<ticks,
              "event ranges");
        ++voices[events[i].voice];
        if(i)
        {
            check(events[i-1].tick<=events[i].tick,"score tick order");
            if(events[i-1].tick==events[i].tick)
            {
                ++ties;
                check(events[i-1].voice<=events[i].voice,"stable left-before-right simultaneous events");
            }
        }
    }
    check(voices[0] && voices[1] && ties,"both hands and simultaneous merged score events");
    printf("INFO: %u waveform bytes, 10 descriptors, %u events, %u song ticks\n",bytes,count,ticks);
}
static void wire_byte(unsigned int byte)
{
    check(wireSize<WIRE_CAPACITY && byte<=255,"bounded diagnostic wire byte");
    wire[wireSize++]=(unsigned char)byte;
}
static void wire_word(unsigned int word)
{
    for(int byte=0;byte<4;++byte) wire_byte((word>>(8*byte))&255);
}
static unsigned int encode_wire(int reference)
{
    unsigned int bytes=0,count=0,ticks=0;
    const unsigned char *wave=reference ? towns_reference_raw_wave(&bytes) : dos_sound_wave(&bytes);
    const DosSoundEvent *events=NULL;
    if(reference) towns_reference_score(&count,&ticks);
    else events=dos_sound_events(&count,&ticks);
    check(wave && (reference || events) && bytes>0 && bytes<=WAVE_CAPACITY &&
          count>0 && count<=EVENT_CAPACITY && ticks<=65535,"diagnostic wire input bounds");
    wireSize=0;
    /* Diagnostic-only schema, also used by the guest; never a game save. */
    wire_word(0x41534344u); wire_word(1); wire_word(bytes); wire_word(count);
    wire_word(ticks);
    for(int id=0;id<NUM_SFX;++id)
    {
        unsigned int fields[4];
        if(reference) towns_reference_sample(id,fields);
        else
        {
            const DosSoundSample *sample=dos_sound_sample(id);
            check(sample!=NULL,"wire sample getter");
            fields[0]=sample->offset; fields[1]=sample->frames;
            fields[2]=sample->pitch; fields[3]=sample->volume;
        }
        for(int i=0;i<4;++i) wire_word(fields[i]);
    }
    for(unsigned int i=0;i<bytes;++i) wire_byte(reference ? unsigned_sample(wave[i]) : wave[i]);
    for(unsigned int i=0;i<count;++i)
    {
        unsigned int fields[3];
        if(reference) towns_reference_event(i,fields);
        else { fields[0]=events[i].tick; fields[1]=events[i].note; fields[2]=events[i].voice; }
        check(fields[0]<=65535 && fields[1]<=255 && fields[2]<=255,"wire event widths");
        wire_byte(fields[0]&255); wire_byte(fields[0]>>8);
        wire_byte(fields[1]); wire_byte(fields[2]);
    }
    check(wireSize==20+10*16+bytes+count*4,"diagnostic wire exact length");
    return wireSize;
}
static void write_wire(const char *path,int reference)
{
    unsigned int bytes=encode_wire(reference);
    FILE *file=fopen(path,"wb"); check(file!=NULL,"open diagnostic wire output");
    int complete=fwrite(wire,1,bytes,file)==bytes;
    int closed=fclose(file)==0;
    check(complete && closed,"complete diagnostic wire output");
}
static void repeat_builds(void)
{
    build_both(); parity();
    unsigned int bytes=encode_wire(0);
    memcpy(snapshot,wire,bytes);
    for(int pass=0;pass<4;++pass)
    {
        build_both(); parity();
        check(encode_wire(0)==bytes && !memcmp(snapshot,wire,bytes),"repeated DOS builds byte stable");
        check(encode_wire(1)==bytes && !memcmp(snapshot,wire,bytes),"repeated fresh-process reference byte stable");
    }
}
int main(int argc,char **argv)
{
    check(argc>=2,"one named test required"); caseName=argv[1];
    if(!strcmp(caseName,"before_build")) before_build();
    else if(!strcmp(caseName,"invalid_ids")) invalid_ids();
    else if(!strcmp(caseName,"parity")) { build_both(); parity(); }
    else if(!strcmp(caseName,"repeat_builds")) repeat_builds();
    else if(!strcmp(caseName,"reference_wire"))
    {
        check(argc==3,"reference wire test needs one output path");
        towns_reference_build(); write_wire(argv[2],1);
    }
    else if(!strcmp(caseName,"wire"))
    {
        check(argc==4,"wire test needs assets and reference output paths");
        build_both(); parity(); write_wire(argv[2],0); write_wire(argv[3],1);
    }
    else check(0,"unknown named test");
    printf("PASS: pinned Towns audio assets %s\n",caseName);
    return 0;
}
