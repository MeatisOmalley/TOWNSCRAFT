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
static DosSoundSample legacy={0,8,256,255};
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
const unsigned char *dos_sound_wave(unsigned int *n) { if(n)*n=8;return wave; }
const DosSoundSample *dos_sound_sample(int id) { assert(id>=0 && id<NUM_SFX);return &legacy; }
int main(int argc,char **argv)
{
    assert(argc==2);scenario=atoi(argv[1]);sound_init();assert(tick && initialized);
    assert(backend==(scenario==2 ? 3 : scenario==1 ? 1 : 2));
    sound_play(-1,256,255,0);assert(!dos_audio_events);
    sound_set_listener(0,0,0,0);sound_play_at(SFX_STONE,256,255,20*4096,0,0);assert(!dos_audio_events);
    dos_sound_step(B_GRASS,200);assert(dos_audio_events==1);
    if(scenario==2) {
        assert(clock_owner && (speaker_port&3)==3);
        for(int i=0;i<12;++i)tick();assert(!(speaker_port&3));
    } else {
        unsigned char out[512];provider(out,sizeof(out));int audible=0;
        for(int i=0;i<512;++i)audible|=out[i]!=128;assert(audible);
        sound_loop(SFX_RAIN,100);assert(loop_started);
        g_sfxOn=0;tick();assert(!loop_started);provider(out,sizeof(out));
        for(int i=0;i<512;++i)assert(out[i]==128);
        unsigned int events=dos_audio_events;sound_play(SFX_STONE,256,255,0);assert(dos_audio_events==events);
        g_sfxOn=1;sound_loop(SFX_RAIN,100);assert(loop_started);
        if(!scenario) {
            int last=-1;
            for(int i=0;i<32;++i) { sound_play(SFX_STONE,256,255,0);assert(last_variant[A_STONE]!=last);last=last_variant[A_STONE]; }
        }
    }
    dos_audio_shutdown();assert(!tick && !clock_owner && !initialized && shut);
    puts("PASS: sampled/basic/speaker selection, mute, rain restart, material steps, position and cleanup");return 0;
}
