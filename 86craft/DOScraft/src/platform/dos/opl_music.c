/* Music on the OPL2 FM chip: the original Towns piece, plus any songs in an
 * external MUSIC.PAK (tools/build_music_pack.py).
 *
 * The Towns played its score on a 4-operator YM2612 electric piano. OPL2 has
 * two operators per channel: a modulator whose brightness decays faster than
 * the carrier gives the same bell-then-body piano. Each hand rotates over 3
 * channels, so notes ring on like a sustain pedal; the Towns sequencer is
 * ported unchanged. Pack songs are pre-allocated to the 9 channels with their
 * own patches, so playing them is replaying timed register writes. After each
 * piece, a random gap, then a random piece (never the same one twice running).
 * Register writes happen in the 100 Hz timer IRQ (a few per note) and in
 * foreground calls that hold the interrupt guard. */
#include "opl_music.h"
#include "sound.h"
#include "sys.h"
#include "sound_assets.h"
#include "keyboard.h"
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <dpmi.h>
#endif

#define OPL_PORT 0x388
#define CHANNELS 9
#define PACK_HEADER 32u
#define PACK_PATCH 12u
#define PACK_SONG 32u
#define PACK_EVENT 5u
#define PACK_LIMIT (256u*1024u)
#define SONG_TAIL 300           /* Ticks for the last notes to ring out */
#define BUILTIN (-1)
unsigned int dos_music_notes;
static int ready;
static const DosSoundEvent *events;
static unsigned int event_count,song_ticks;
static volatile int playing,wait_ticks=-1;
static int gap=6000,music_tick,next_event,rotation[2];
static int current=BUILTIN,last_piece=-2;
static unsigned char keyed[CHANNELS],carrier_level[CHANNELS];
/* Loaded pack: patch, song and event tables inside one allocation. */
static unsigned char *pack;
static const unsigned char *pack_patches,*pack_songs,*pack_events;
static unsigned int pack_song_count,pack_patch_count,pack_builtin;
static const unsigned char *song;
static unsigned int song_events,song_next,song_wait,song_tail;
/* Hard disk first, like SOUND.PAK, then the boot floppy. */
static const char *pack_paths[2]={"C:\\MUSIC.PAK","A:\\MUSIC.PAK"};

/* Operator register offsets of channels 0-8; the carrier is 3 above. */
static const unsigned char operator_offset[CHANNELS]={0,1,2,8,9,10,16,17,18};
/* Towns electric piano: modulator then carrier 20h (MUL), 40h (TL), 60h
 * (AR/DR), 80h (SL/RR), E0h (wave), then C0h (feedback 3, FM). */
static const unsigned char towns_patch[PACK_PATCH]={0x01,0x1A,0xF3,0x35,0x00,0x01,0x04,0xF2,0x24,0x00,0x06,0};
/* F-numbers of C4..B4 at block 4: f * 2^16 / 49716 Hz. */
static const unsigned short fnum_table[12]={345,365,387,410,435,460,488,517,547,580,615,651};

static unsigned int u16le(const unsigned char *p) { return p[0]|((unsigned)p[1]<<8); }
static unsigned int u32le(const unsigned char *p) { return u16le(p)|(u16le(p+2)<<16); }

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

static void set_patch(int ch,const unsigned char *patch)
{
    static const unsigned char base[5]={0x20,0x40,0x60,0x80,0xE0};
    for(int r=0;r<5;++r) {
        opl_write(base[r]+operator_offset[ch],patch[r]);
        opl_write(base[r]+operator_offset[ch]+3,patch[5+r]);
    }
    opl_write(0xC0+ch,patch[10]);
    carrier_level[ch]=patch[6]&63;
}

static void all_off(void)
{
    for(int ch=0;ch<CHANNELS;++ch)if(keyed[ch]&0x20) { keyed[ch]&=~0x20;opl_write(0xB0+ch,keyed[ch]); }
}

static void key_off(int ch)
{
    if(keyed[ch]&0x20) { keyed[ch]&=~0x20;opl_write(0xB0+ch,keyed[ch]); }
}

/* attenuation: 0.75 dB steps added to the patch's carrier level. */
static void note(int ch,int midi,int attenuation)
{
    int block=midi/12-1,fnum=fnum_table[midi%12],level=carrier_level[ch]+attenuation;
    if(block<0)block=0;
    if(block>7)block=7;
    opl_write(0xB0+ch,(block<<2)|(fnum>>8));    /* Key off before the retrigger */
    opl_write(0x40+operator_offset[ch]+3,level>63 ? 63 : level);
    opl_write(0xA0+ch,fnum&255);
    keyed[ch]=(unsigned char)(0x20|(block<<2)|(fnum>>8));
    opl_write(0xB0+ch,keyed[ch]);
    ++dos_music_notes;
}

/* Checks every table and event once, so the IRQ can trust the pack. */
static int pack_valid(const unsigned char *data,unsigned int bytes)
{
    if(bytes<PACK_HEADER || memcmp(data,"DCMUS01\0",8) || u32le(data+8)!=1)return 0;
    unsigned int songs=u32le(data+16),patches=u32le(data+20),payload=u32le(data+24);
    if(!songs || songs>32 || !patches || patches>32 || payload!=bytes-PACK_HEADER)return 0;
    unsigned int h=2166136261u;
    for(unsigned int i=PACK_HEADER;i<bytes;++i)h=(h^data[i])*16777619u;
    unsigned int tables=patches*PACK_PATCH+songs*PACK_SONG;
    if(h!=u32le(data+28) || tables>payload)return 0;
    const unsigned char *event_base=data+PACK_HEADER+tables;
    unsigned int event_bytes=payload-tables;
    for(unsigned int s=0;s<songs;++s) {
        const unsigned char *d=data+PACK_HEADER+patches*PACK_PATCH+s*PACK_SONG;
        unsigned int offset=u32le(d+20),count=u32le(d+24);
        if(!count || count>65535 || offset>event_bytes || count*PACK_EVENT>event_bytes-offset)return 0;
        for(unsigned int i=0;i<count;++i) {
            const unsigned char *e=event_base+offset+i*PACK_EVENT;
            unsigned int kind=e[2]>>4,ch=e[2]&15;
            if(kind>3 || ch>=CHANNELS || (kind==1 && (e[3]<12 || e[3]>107 || e[4]>63)) ||
               (kind==2 && e[3]>=patches))return 0;
        }
    }
    return 1;
}

static int load_pack(const char *path)
{
    FILE *f=fopen(path,"rb");
    if(!f)return -1;
    long size=fseek(f,0,SEEK_END) ? -1 : ftell(f);
    unsigned char *data=size>=(long)PACK_HEADER && size<=(long)PACK_LIMIT && !fseek(f,0,SEEK_SET) ?
                        malloc((size_t)size) : 0;
    int ok=data && fread(data,1,(size_t)size,f)==(size_t)size && pack_valid(data,(unsigned)size);
    fclose(f);
    if(!ok) { free(data);return -1; }
#ifdef __DJGPP__
    _go32_dpmi_lock_data(data,(unsigned long)size);    /* Read by the timer IRQ */
#endif
    pack=data;pack_builtin=u32le(data+12)&1;
    pack_song_count=u32le(data+16);pack_patch_count=u32le(data+20);
    pack_patches=data+PACK_HEADER;
    pack_songs=pack_patches+pack_patch_count*PACK_PATCH;
    pack_events=pack_songs+pack_song_count*PACK_SONG;
    return 0;
}

int dos_music_init(void)
{
    ready=0;dos_music_notes=0;current=BUILTIN;last_piece=-2;
    events=dos_sound_events(&event_count,&song_ticks);
    if(!events)event_count=0;
    free(pack);pack=0;pack_song_count=0;
    for(int i=0;i<2 && !pack;++i)load_pack(pack_paths[i]);
    if((!event_count && !pack_song_count) || !opl_detect()) {
        free(pack);pack=0;pack_song_count=0;
        return -1;
    }
    for(int reg=1;reg<=0xF5;++reg)opl_write(reg,0);
    opl_write(1,0x20);                          /* Allow waveform select */
    for(int ch=0;ch<CHANNELS;++ch) { set_patch(ch,towns_patch);keyed[ch]=0; }
    playing=0;rotation[0]=rotation[1]=0;
    ready=1;
    return 0;
}

const char *dos_music_backend(void) { return ready ? "OPL2" : "NONE"; }
unsigned int dos_music_songs(void) { return ready ? pack_song_count : 0; }
int dos_music_current(void) { return current; }
int dos_music_playing(void) { return ready && playing; }
const char *dos_music_song_name(int piece)
{
    static char name[21];
    if(piece<0 || (unsigned)piece>=pack_song_count)return "Towns piece";
    memcpy(name,pack_songs+(unsigned)piece*PACK_SONG,20);
    name[20]=0;
    return name;
}

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

/* A random piece, never the previous one when there is a choice. */
static int choose_piece(void)
{
#ifdef DOS_MUSIC_JUKEBOX
    /* Listening build: pack songs in file order, then around again. */
    if(pack_song_count)return last_piece<0 ? 0 : (last_piece+1)%(int)pack_song_count;
#endif
    int builtin=event_count && (pack_builtin || !pack_song_count);
    unsigned int choices=pack_song_count+(builtin ? 1u : 0u);
    unsigned int pick=((unsigned)g_ticks*2654435761u>>16)%choices;
    int piece=builtin && pick==pack_song_count ? BUILTIN : (int)pick;
    if(choices>1 && piece==last_piece) {
        pick=(pick+1)%choices;
        piece=builtin && pick==pack_song_count ? BUILTIN : (int)pick;
    }
    return piece;
}

static void start_piece(void)
{
    current=choose_piece();last_piece=current;
    playing=1;music_tick=0;next_event=0;
    if(current==BUILTIN) {
        for(int ch=0;ch<6;++ch)set_patch(ch,towns_patch);
        return;
    }
    song=pack_songs+(unsigned)current*PACK_SONG;
    song_events=u32le(song+24);song_next=0;song_tail=SONG_TAIL;
    song_wait=u16le(pack_events+u32le(song+20));
}

static void finish_piece(void)
{
    all_off();playing=0;
#ifdef DOS_MUSIC_JUKEBOX
    wait_ticks=100;                             /* One second between songs */
#else
    wait_ticks=gap+(int)(((unsigned)g_ticks*2654435761u>>16)%(unsigned)(gap+1));
#endif
}

static void pack_tick(void)
{
    const unsigned char *base=pack_events+u32le(song+20);
    while(song_next<song_events && !song_wait) {
        const unsigned char *e=base+song_next*PACK_EVENT;
        int kind=e[2]>>4,ch=e[2]&15;
        if(kind==0)key_off(ch);
        else if(kind==1)note(ch,e[3],e[4]);
        else if(kind==2)set_patch(ch,pack_patches+e[3]*PACK_PATCH);
        if(++song_next<song_events)song_wait=u16le(base+song_next*PACK_EVENT);
    }
    if(song_next<song_events)--song_wait;
    else if(!song_tail--)finish_piece();
}

void dos_music_tick(void)
{
    if(!ready)return;
#ifdef DOS_MUSIC_JUKEBOX
    /* F6 skips to the next song at once, on any screen; music stays on. */
    static int f6_held;
    int f6=g_keyDown[KEY_PF6]!=0;
    g_musicOn=1;
    if(f6 && !f6_held) {
        if(playing)all_off();
        wait_ticks=-1;start_piece();
    }
    f6_held=f6;
#endif
    if(!playing) {
        if(wait_ticks>0)--wait_ticks;
        else if(!wait_ticks && g_musicOn) { wait_ticks=-1;start_piece(); }
        return;
    }
    if(!g_musicOn) { all_off();playing=0;wait_ticks=gap;return; }
    if(current!=BUILTIN) { pack_tick();return; }
    while((unsigned)next_event<event_count && events[next_event].tick<=music_tick) {
        int voice=events[next_event].voice,ch=voice*3+rotation[voice];
        rotation[voice]=(rotation[voice]+1)%3;
        note(ch,events[next_event].note,(127-(voice ? 112 : 84))>>2);  /* Melody over the accompaniment */
        ++next_event;
    }
    if((unsigned)++music_tick>song_ticks+400)finish_piece();  /* Let the last chord ring out */
}

void dos_music_shutdown(void)
{
    if(!ready)return;
    ready=0;playing=0;wait_ticks=-1;
    all_off();
    for(int ch=0;ch<CHANNELS;++ch)opl_write(0x40+operator_offset[ch]+3,63);  /* Silence release tails */
}
