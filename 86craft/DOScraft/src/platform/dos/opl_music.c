/* Music on the OPL2 FM chip, the Towns sequencer ported unchanged.
 *
 * The Towns played its score on a 4-operator YM2612 electric piano. OPL2 has
 * two operators per channel: a modulator whose brightness decays faster than
 * the carrier gives the same bell-then-body piano. Each hand rotates over 3
 * channels, so notes ring on like a sustain pedal. Register writes happen in
 * the 100 Hz timer IRQ (a few per note) and in foreground calls that hold the
 * interrupt guard. */
#include "opl_music.h"
#include "sound.h"
#include "sys.h"
#include "sound_assets.h"
#include "keyboard.h"
#include <pc.h>

#define OPL_PORT 0x388
unsigned int dos_music_notes;
static int ready;
static const DosSoundEvent *events;
static unsigned int event_count,song_ticks;
static volatile int playing,wait_ticks=-1;
static int gap=6000,music_tick,next_event,rotation[2];
static unsigned char keyed[6];

/* Operator register offsets of channels 0-8; the carrier is 3 above. */
static const unsigned char operator_offset[9]={0,1,2,8,9,10,16,17,18};
/* Registers 20h (MUL), 40h (TL), 60h (AR/DR), 80h (SL/RR), E0h (wave). */
static const unsigned char modulator[5]={0x01,0x1A,0xF3,0x35,0x00};
static const unsigned char carrier[5]={0x01,0x04,0xF2,0x24,0x00};
/* F-numbers of C4..B4 at block 4: f * 2^16 / 49716 Hz. */
static const unsigned short fnum_table[12]={345,365,387,410,435,460,488,517,547,580,615,651};

static void opl_write(int reg,int value)
{
    outportb(OPL_PORT,reg);
    for(int i=0;i<6;++i)inportb(OPL_PORT);      /* 3.3 us after the index */
    outportb(OPL_PORT+1,value);
    for(int i=0;i<35;++i)inportb(OPL_PORT);     /* 23 us after the data */
}

static int opl_detect(void)
{
    opl_write(4,0x60);opl_write(4,0x80);
    int before=inportb(OPL_PORT)&0xE0;
    opl_write(2,0xFF);opl_write(4,0x21);
    for(int i=0;i<200;++i)inportb(OPL_PORT);    /* Timer 1 overflows after 80 us */
    int after=inportb(OPL_PORT)&0xE0;
    opl_write(4,0x60);opl_write(4,0x80);
    return !before && after==0xC0;
}

static void set_patch(int ch)
{
    static const unsigned char base[5]={0x20,0x40,0x60,0x80,0xE0};
    for(int r=0;r<5;++r) {
        opl_write(base[r]+operator_offset[ch],modulator[r]);
        opl_write(base[r]+operator_offset[ch]+3,carrier[r]);
    }
    opl_write(0xC0+ch,(3<<1)|0);                /* Feedback 3, FM */
}

static void all_off(void)
{
    for(int ch=0;ch<6;++ch)if(keyed[ch]&0x20) { keyed[ch]&=~0x20;opl_write(0xB0+ch,keyed[ch]); }
}

static void note(int ch,int midi,int volume)
{
    int block=midi/12-1,fnum=fnum_table[midi%12];
    if(block<0)block=0;
    if(block>7)block=7;
    opl_write(0xB0+ch,(block<<2)|(fnum>>8));    /* Key off before the retrigger */
    opl_write(0x40+operator_offset[ch]+3,(carrier[1]+((127-volume)>>2))&63);
    opl_write(0xA0+ch,fnum&255);
    keyed[ch]=(unsigned char)(0x20|(block<<2)|(fnum>>8));
    opl_write(0xB0+ch,keyed[ch]);
    ++dos_music_notes;
}

int dos_music_init(void)
{
    ready=0;dos_music_notes=0;
    events=dos_sound_events(&event_count,&song_ticks);
    if(!events || !event_count || !opl_detect())return -1;
    for(int reg=1;reg<=0xF5;++reg)opl_write(reg,0);
    opl_write(1,0x20);                          /* Allow waveform select */
    for(int ch=0;ch<9;++ch)set_patch(ch);
    for(int ch=0;ch<6;++ch)keyed[ch]=0;
    playing=0;rotation[0]=rotation[1]=0;
    ready=1;
    return 0;
}

const char *dos_music_backend(void) { return ready ? "OPL2" : "NONE"; }

void music_schedule(int ticks)
{
    unsigned long state=dos_keyboard_irq_save();
    if(!playing)wait_ticks=ticks;
    dos_keyboard_irq_restore(state);
}

void music_set_gap(int minimum) { gap=minimum; }

void music_stop(void)
{
    unsigned long state=dos_keyboard_irq_save();
    if(playing && ready)all_off();
    playing=0;wait_ticks=-1;
    dos_keyboard_irq_restore(state);
}

void dos_music_tick(void)
{
    if(!ready)return;
    if(!playing) {
        if(wait_ticks>0)--wait_ticks;
        else if(!wait_ticks && g_musicOn) { wait_ticks=-1;playing=1;music_tick=0;next_event=0; }
        return;
    }
    if(!g_musicOn) { all_off();playing=0;wait_ticks=gap;return; }
    while((unsigned)next_event<event_count && events[next_event].tick<=music_tick) {
        int voice=events[next_event].voice,ch=voice*3+rotation[voice];
        rotation[voice]=(rotation[voice]+1)%3;
        note(ch,events[next_event].note,voice ? 112 : 84);  /* Melody over the accompaniment */
        ++next_event;
    }
    if((unsigned)++music_tick>song_ticks+400) {         /* Let the last chord ring out */
        all_off();playing=0;
        wait_ticks=gap+(int)(((unsigned)g_ticks*2654435761u>>16)%(unsigned)(gap+1));
    }
}

void dos_music_shutdown(void)
{
    if(!ready)return;
    ready=0;playing=0;wait_ticks=-1;
    all_off();
    for(int ch=0;ch<9;++ch)opl_write(0x40+operator_offset[ch]+3,63);  /* Silence release tails */
}
