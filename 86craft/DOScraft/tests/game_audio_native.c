#undef NDEBUG
#define DOS_SB_STREAM
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "game_audio.c"
static int scenario,clock_owner,shut,irq_enabled=1;
static void (*tick)(void);
static void (*provider)(unsigned char *,unsigned int);
static unsigned char speaker_port;
static unsigned char wave[]={0,128,255,128,0,128,255,128};
static DosAudioClip clip={wave,8,11025};
static DosSoundSample legacy[NUM_SFX];
/* Towns effect levels: crunch stone wood step place hurt explode hiss click rain */
static const unsigned char towns_level[NUM_SFX]={200,170,230,120,230,190,255,110,140,150};
static int music_inits,music_ticks,music_shut;
int dos_music_init(void) { ++music_inits;return 0; }
void dos_music_tick(void) { ++music_ticks; }
void dos_music_shutdown(void) { ++music_shut; }
unsigned long dos_keyboard_irq_save(void) { int old=irq_enabled;irq_enabled=0;return old; }
void dos_keyboard_irq_restore(unsigned long old) { irq_enabled=(int)old; }
void dos_irq_set_tick_hook(void (*fn)(void)) { tick=fn; }
void dos_video_set_speaker_clock(int on) { clock_owner=on; }
int dos_sb_init(void) { return scenario==2 ? -1 : 0; }
void dos_sb_shutdown(void) { ++shut; }
int dos_sb_stream_start(void (*fn)(unsigned char *,unsigned int)) { provider=fn;return 0; }
unsigned char inportb(unsigned short port) { assert(port==0x61);return speaker_port; }
void outportb(unsigned short port,unsigned char value)
{ assert(port==0x43 || port==0x42 || port==0x61);if(port==0x61)speaker_port=value; }
int dos_audio_bank_load(const char *path) { (void)path;return scenario==1 ? -1 : 0; }
unsigned int dos_audio_bank_count(int group) { assert(group>=0 && group<A_GROUPS);return 3; }
const DosAudioClip *dos_audio_bank_clip(int group,unsigned int variant)
{ assert(group>=0 && group<A_GROUPS && variant<3);return &clip; }
int dos_sound_assets_build(void) { return 0; }
int dos_sound_level(int id) { assert(id>=0 && id<NUM_SFX);return towns_level[id]; }
const unsigned char *dos_sound_wave(unsigned int *n) { if(n)*n=8;return wave; }
const DosSoundSample *dos_sound_sample(int id)
{ assert(id>=0 && id<NUM_SFX);legacy[id]=(DosSoundSample){0,8,256,towns_level[id]};return &legacy[id]; }
static int first_output(void)
{ unsigned char out[4];provider(out,sizeof(out));dos_audio_voices_stop();return out[0]; }
int main(int argc,char **argv)
{
    assert(argc==2);scenario=atoi(argv[1]);sound_init();assert(tick && initialized && music_inits==1);
    tick();assert(music_ticks==1);
    assert(backend==(scenario==2 ? 3 : scenario==1 ? 1 : 2));
    sound_play(-1,256,255,0);assert(!dos_audio_events);
    sound_set_listener(0,0,0,0);sound_play_at(SFX_STONE,256,255,20*4096,0,0);assert(!dos_audio_events);
    dos_sound_step(B_GRASS,200);assert(dos_audio_events==1);
    if(scenario==2) {
        assert(clock_owner && (speaker_port&3)==3);
        for(int i=0;i<12;++i)tick();
        unsigned int before=dos_audio_events;
        for(int i=0;i<2400;++i)dos_sound_zombie(0,0,0);assert(dos_audio_events==before);  /* No groan beeps */
        dos_sound_step(B_GRASS,200);
        for(int i=0;i<12;++i)tick();assert(!(speaker_port&3));
    } else {
        unsigned char out[512];provider(out,sizeof(out));int audible=0;
        for(int i=0;i<512;++i)audible|=out[i]!=128;assert(audible);
        sound_loop(SFX_RAIN,100);assert(loop_started);
        g_sfxOn=0;tick();assert(!loop_started);provider(out,sizeof(out));
        for(int i=0;i<512;++i)assert(out[i]==128);
        unsigned int events=dos_audio_events;sound_play(SFX_STONE,256,255,0);assert(dos_audio_events==events);
        g_sfxOn=1;sound_loop(SFX_RAIN,100);assert(loop_started);
        /* Original balance on the shared clip: stone 255*170/256*4/5=135 and
           step 255*120/256*4/5=95 scale the first sample (-128). */
        g_sfxOn=1;sound_loop(SFX_RAIN,0);dos_audio_voices_stop();
        sound_play(SFX_STONE,256,255,0);assert(first_output()==128-128*135/256);
        sound_play(SFX_STEP,256,255,0);assert(first_output()==128-128*95/256);
        sound_play(SFX_EXPLODE,256,255,0);assert(first_output()==128+dos_audio_soft_clip(-128*(255*255/256*4/5)/256));  /* Past the knee */
        if(!scenario) {
            unsigned int before=dos_audio_events;
            for(int i=0;i<2400;++i) { dos_sound_zombie(0,0,0);dos_audio_voices_stop(); }
            unsigned int groans=dos_audio_events-before;assert(groans>=8 && groans<=40);  /* ~20 */
            before=dos_audio_events;
            for(int i=0;i<2400;++i)dos_sound_zombie(17*4096,0,0);           /* Beyond 16 blocks */
            assert(dos_audio_events==before);
        } else {
            unsigned int before=dos_audio_events;   /* No synthesized groan */
            for(int i=0;i<2400;++i)dos_sound_zombie(0,0,0);
            assert(dos_audio_events==before);
        }
        if(!scenario) {
            int last=-1;
            for(int i=0;i<32;++i) { sound_play(SFX_STONE,256,255,0);assert(last_variant[A_STONE]!=last);last=last_variant[A_STONE]; }
        }
    }
    dos_audio_shutdown();assert(!tick && !clock_owner && !initialized && shut && music_shut==1);
    puts("PASS: sampled/basic/speaker selection, mute, rain restart, material steps, Towns balance, groans, music hooks, position and cleanup");return 0;
}
