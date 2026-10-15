#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opl_music.c"
/* A minimal OPL2 at 388h: registers, and timer 1 status for detection. */
static int present,index_reg,irq_enabled=1;
static unsigned char reg[256],status;
static unsigned int writes;
volatile u32 g_ticks;
int g_musicOn=1,g_sfxOn=1;
unsigned long dos_keyboard_irq_save(void) { int old=irq_enabled;irq_enabled=0;return old; }
void dos_keyboard_irq_restore(unsigned long old) { irq_enabled=(int)old; }
unsigned char inportb(unsigned short port)
{ assert(port==OPL_PORT);return present ? status : 0xFF; }
void outportb(unsigned short port,unsigned char value)
{
    assert(port==OPL_PORT || port==OPL_PORT+1);
    if(port==OPL_PORT) { index_reg=value;return; }
    reg[index_reg]=value;++writes;
    if(index_reg==4) {
        if(value&0x80)status=0;
        else if((value&1) && !(value&0x40))status=0xC0;
    }
}
/* Two eighths per hand (EIGHTH = 36 ticks), then the song ends at 72. */
static const DosSoundEvent score[]={{0,60,0},{0,72,1},{36,62,0},{36,74,1}};
static int have_score=1;
const DosSoundEvent *dos_sound_events(unsigned int *count,unsigned int *song)
{
    if(count)*count=have_score ? 4 : 0;
    if(song)*song=72;
    return have_score ? score : 0;
}
static int keyed_on(int ch) { return reg[0xB0+ch]&0x20; }
#ifndef OPL_PACK_TEST
int main(void)
{
    /* No OPL: nothing is written after detection and ticks stay inert. */
    present=0;assert(dos_music_init()<0 && !strcmp(dos_music_backend(),"NONE"));
    unsigned int before=writes;music_schedule(0);for(int i=0;i<200;++i)dos_music_tick();
    music_stop();dos_music_shutdown();assert(writes==before && !dos_music_notes);
    /* No score: detection is not even tried. */
    present=1;have_score=0;assert(dos_music_init()<0);have_score=1;

    assert(!dos_music_init() && !strcmp(dos_music_backend(),"OPL2"));
    assert(reg[1]==0x20 && reg[0xC0]==0x06 && reg[0x20]==0x01 && reg[0x23]==0x01);
    assert(reg[0x43]==0x04 && reg[0x40]==0x1A && reg[0x60]==0xF3 && reg[0x63]==0xF2);
    for(int ch=0;ch<9;++ch)assert(!keyed_on(ch) && reg[0xC0+ch]==0x06);

    music_set_gap(1000);music_schedule(3);assert(irq_enabled);
    for(int i=0;i<3;++i)dos_music_tick();
    assert(!playing && !dos_music_notes);
    dos_music_tick();assert(playing && !dos_music_notes);   /* Starts; notes from the next tick */
    dos_music_tick();
    /* Accompaniment C4 on channel 0 (block 4, F 345), melody C5 on channel 3. */
    assert(dos_music_notes==2 && keyed_on(0) && keyed_on(3));
    assert(reg[0xA0]==(345&255) && reg[0xB0]==(0x20|(4<<2)|(345>>8)));
    assert(reg[0xA3]==(345&255) && reg[0xB3]==(0x20|(5<<2)|(345>>8)));
    assert(reg[0x43]==4+((127-84)>>2) && reg[0x4B]==4+((127-112)>>2));
    for(int i=0;i<36;++i)dos_music_tick();
    /* The next notes rotate to channels 1 and 4; the first ones ring on. */
    assert(dos_music_notes==4 && keyed_on(1) && keyed_on(4) && keyed_on(0) && keyed_on(3));
    assert(reg[0xA1]==(387&255) && reg[0xB4]==(0x20|(5<<2)|(387>>8)));

    /* Turning music off silences it and waits a full gap before replaying. */
    g_musicOn=0;dos_music_tick();
    for(int ch=0;ch<6;++ch)assert(!keyed_on(ch));
    assert(!playing && wait_ticks==1000);
    g_musicOn=1;for(int i=0;i<1000;++i)dos_music_tick();
    assert(!playing);dos_music_tick();assert(playing);
    /* The piece ends 400 ticks after its last event, then rests gap..2*gap. */
    for(int i=0;i<72+401;++i)dos_music_tick();  /* Tick 473 passes 72+400 */
    assert(!playing && wait_ticks>=1000 && wait_ticks<=2000);
    for(int ch=0;ch<6;++ch)assert(!keyed_on(ch));

    /* music_stop cancels a pending start; shutdown silences release tails. */
    music_schedule(5);music_stop();for(int i=0;i<20;++i)dos_music_tick();assert(!playing);
    music_schedule(0);dos_music_tick();dos_music_tick();assert(playing && keyed_on(1));  /* Rotation carries over: 2+2 notes */
    dos_music_shutdown();
    for(int ch=0;ch<6;++ch)assert(!keyed_on(ch));
    for(int ch=0;ch<9;++ch)assert(reg[0x40+operator_offset[ch]+3]==63);
    before=writes;dos_music_tick();assert(writes==before && !strcmp(dos_music_backend(),"NONE"));
    puts("PASS: OPL2 detection, patch, note pitch/volume, hand rotation, toggle, ending gap, stop and shutdown");
    return 0;
}
#else
/* A pack converted from the test's own MIDI: piano C4 for 0.5 s, then a pad
 * E4+G4 for 0.25 s (tools/build_music_pack.py, no Towns piece). */
int main(int argc,char **argv)
{
    assert(argc==3);present=1;
    pack_paths[0]=argv[2];pack_paths[1]="missing.pak";
    assert(!dos_music_init() && !dos_music_songs());          /* Corrupt pack: Towns piece only */
    pack_paths[0]=argv[1];
    assert(!dos_music_init() && dos_music_songs()==1);
    music_schedule(0);dos_music_tick();assert(playing && dos_music_current()==0 && !dos_music_notes);
    dos_music_tick();
    assert(dos_music_notes==1 && keyed_on(0) && !keyed_on(1));
    assert(reg[0x20]==0x01 && reg[0x60]==0xF2 && reg[0x63]==0xF2 && reg[0xC0]==0x06);  /* Piano */
    assert(reg[0xA0]==(345&255) && reg[0xB0]==(0x20|(4<<2)|(345>>8)) && reg[0x43]==0 && reg[0x40]==0x18);  /* Loudest note: full level */
    for(int i=1;i<50;++i)dos_music_tick();
    assert(keyed_on(0) && dos_music_notes==1);                  /* Exactly 0.5 s */
    dos_music_tick();
    assert(!keyed_on(0) && keyed_on(1) && keyed_on(2) && dos_music_notes==3);
    assert(reg[0x21]==0x21 && reg[0x62]==0x62 && reg[0xC1]==0x08 && reg[0xC2]==0x08);  /* Pad */
    assert(reg[0xA1]==(435&255) && reg[0xA2]==(517&255));
    for(int i=0;i<25;++i)dos_music_tick();
    assert(!keyed_on(1) && !keyed_on(2) && playing);
    for(int i=0;i<299;++i)dos_music_tick();
    assert(playing);                                            /* Release tail */
    dos_music_tick();
    assert(!playing && wait_ticks>=6000 && wait_ticks<=12000);
    /* With the Towns piece in the rotation, the same piece never plays twice running. */
    pack_builtin=1;
    int last=dos_music_current();
    for(int i=0;i<6;++i) {
        music_stop();music_schedule(0);g_ticks+=977u*(unsigned)i;dos_music_tick();
        assert(playing && dos_music_current()!=last);last=dos_music_current();
    }
    /* Level byte: output quieter by bits 0-5, modulator darker by bits 6-7
     * (3 dB = 4 steps each); key-scale bits survive. */
    static const unsigned char fm[12]={0x01,0x58,0xF2,0x54,0,0x01,0x40,0xF2,0x24,0,0x06,0};
    static const unsigned char add[12]={0x22,0x0A,0xF0,0x0F,0,0x21,0x00,0xF0,0x0F,0,0x01,0};
    set_patch(7,fm);note(7,60,8|(2<<6));
    assert(reg[0x40+operator_offset[7]]==(0x40|(0x18+8)) && reg[0x40+operator_offset[7]+3]==(0x40|8));
    set_patch(8,add);note(8,60,8|(2<<6));
    assert(reg[0x40+operator_offset[8]]==0x0A+8 && reg[0x40+operator_offset[8]+3]==8);  /* Additive: both quieter */
    note(7,60,60|(3<<6));
    assert((reg[0x40+operator_offset[7]+3]&63)==60 && (reg[0x40+operator_offset[7]]&63)==0x18+12);
    dos_music_shutdown();
    for(int ch=0;ch<9;++ch)assert(!keyed_on(ch) && reg[0x40+operator_offset[ch]+3]==63);
    puts("PASS: pack load/reject, patches, pitch, level, timing, tail, rotation and shutdown");
    return 0;
}
#endif
