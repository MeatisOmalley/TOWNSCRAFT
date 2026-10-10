/* Full pinned Towns sound.c reference, compiled in its own translation unit.
   test_sound_assets.py makes one recorded unique PCM_WINDOW replacement and
   supplies declaration-only hw.h. No production source or hardware operation
   is used here. The only mocks are banked wave RAM, port reads/writes and IRQ
   declarations; original synthesis, parameters, parser and sort run intact. */
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "reference_original_sound.c"

enum { WAVE_CAPACITY=65536, GUARD_WORDS=8 };
static struct {
    u32 before[GUARD_WORDS];
    u8 bytes[WAVE_CAPACITY];
    u32 after[GUARD_WORDS];
} referenceRAM;
static int referenceBank=-1;
static unsigned int bankMask,windowWrites,portWrites,portReads;
static int initializing;
volatile u32 g_ticks;

static void require(int ok,const char *message)
{
    if(!ok) { fprintf(stderr,"FAIL Towns audio reference: %s\n",message); exit(1); }
}
static void guards(void)
{
    for(int i=0;i<GUARD_WORDS;++i)
        require(referenceRAM.before[i]==0xA55AA55Au &&
                referenceRAM.after[i]==0x5AA55AA5u,"wave RAM guard words");
}
void outb(u16 port,u8 value)
{
    require(initializing,"unexpected port write outside reference initialization");
    require(++portWrites<4096,"bounded mock port writes");
    require((port>=0x4F0 && port<=0x4F8) || port==0x4D5 || port==0x4EC ||
            port==0x4D8 || port==0x4DA || port==0x4DC || port==0x4DE,
            "unknown mock output port");
    if(port==0x4F7 && !(value&0x40))
    {
        require(value<16,"bounded wave bank selection");
        referenceBank=value;
        bankMask|=1u<<value;
    }
}
u8 inb(u16 port)
{
    require(initializing && (port==0x4D8 || port==0x4EC),"unknown mock input port");
    require(++portReads<4096,"bounded mock port reads");
    return 0; /* FM never busy; audio-enable register initially clear. */
}
void cli(void) { require(0,"unexpected IRQ disable (no playback test)"); }
void sti(void) { require(0,"unexpected IRQ enable (no playback test)"); }
volatile u8 *reference_pcm_window(void)
{
    require(initializing && referenceBank>=0 && referenceBank<16,
            "valid selected wave bank");
    require(synthTop>=0 && synthTop<WAVE_CAPACITY &&
            referenceBank==(synthTop>>12) && synthBank==referenceBank,
            "original sample write matches bounded selected bank");
    require(++windowWrites<=WAVE_CAPACITY,"bounded sample writes");
    return referenceRAM.bytes+referenceBank*4096;
}
void towns_reference_build(void)
{
    /* A fresh-process equivalent for repeat-build parity. Gaps were not played
       on Towns; zero initialization defines them as raw silence for the wire. */
    memset(referenceRAM.bytes,0,sizeof(referenceRAM.bytes));
    for(int i=0;i<GUARD_WORDS;++i)
    {
        referenceRAM.before[i]=0xA55AA55Au;
        referenceRAM.after[i]=0x5AA55AA5u;
    }
    synthSeed=12345; synthTop=0; synthBank=-1; nEvents=songLen=0;
    referenceBank=-1; bankMask=windowWrites=portWrites=portReads=0;
    initializing=1;
    sound_init();
    initializing=0;
    guards();
    require(synthTop>0 && synthTop<=WAVE_CAPACITY,"wave length bound");
    require(nEvents>0 && nEvents<=MAX_EVENTS && songLen>0 && songLen<=65535,
            "score bounds");
    require(bankMask==((1u<<((synthTop+4095)/4096))-1),"every synthesized wave bank selected");
    require(windowWrites>4096 && portWrites && portReads,"actual banked original initialization executed");
}
const unsigned char *towns_reference_raw_wave(unsigned int *bytes)
{
    guards(); *bytes=(unsigned int)synthTop;
    return referenceRAM.bytes;
}
void towns_reference_sample(int id,unsigned int fields[4])
{
    require(id>=0 && id<NUM_SFX,"reference sample ID");
    unsigned int start=(unsigned int)sfx[id].start*256,end=start;
    require(start<(unsigned int)synthTop,"reference sample start");
    while(end<(unsigned int)synthTop && referenceRAM.bytes[end]!=0xFF) ++end;
    require(end<(unsigned int)synthTop && end>start,"reference end marker");
    fields[0]=start; fields[1]=end-start;
    fields[2]=sfx[id].pitch; fields[3]=sfx[id].vol;
}
void towns_reference_score(unsigned int *count,unsigned int *ticks)
{ *count=(unsigned int)nEvents; *ticks=(unsigned int)songLen; }
void towns_reference_event(unsigned int id,unsigned int fields[3])
{
    require(id<(unsigned int)nEvents,"reference event ID");
    fields[0]=evTick[id]; fields[1]=evNote[id]; fields[2]=evVoice[id];
}
