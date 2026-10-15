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
static unsigned int empty_groups;     /* Bit per group the bank leaves empty */
unsigned int dos_audio_bank_count(int group)
{ assert(group>=0 && group<A_GROUPS);return empty_groups>>group&1 ? 0 : 3; }
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
        for(int i=0;i<2400;++i) { dos_sound_mob_tick(MOB_ZOMBIE,0,0,0);dos_sound_splash(-8192);dos_sound_swim(); }
        assert(dos_audio_events==before);                       /* No call or water beeps */
        assert(!dos_sound_hit(B_STONE,0,0,0) && !dos_sound_mob(MOB_PIG,1,0,0,0));   /* Original beeps */
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
            for(int i=0;i<2400;++i) { dos_sound_mob_tick(MOB_ZOMBIE,0,0,0);dos_audio_voices_stop(); }
            unsigned int groans=dos_audio_events-before;assert(groans>=8 && groans<=40);  /* ~20 */
            before=dos_audio_events;
            for(int i=0;i<2400;++i)dos_sound_mob_tick(MOB_ZOMBIE,17*4096,0,0);  /* Beyond 16 blocks */
            for(int i=0;i<2400;++i)dos_sound_mob_tick(MOB_CREEPER,0,0,0);       /* No creeper call */
            for(int i=0;i<2400;++i)dos_sound_mob_tick(MOB_TNT,0,0,0);
            empty_groups=1u<<A_PIG_SAY;
            for(int i=0;i<2400;++i)dos_sound_mob_tick(MOB_PIG,0,0,0);           /* None picked */
            assert(dos_audio_events==before);
            for(int i=0;i<2400;++i) { dos_sound_mob_tick(MOB_CHICKEN,0,0,0);dos_audio_voices_stop(); }
            assert(last_variant[A_CHICKEN_SAY]>=0);
            /* Mining hit: the block's footstep group; sand falls back to grass. */
            before=dos_audio_events;assert(dos_sound_hit(B_STONE,0,0,0) && last_variant[A_STEP_STONE]>=0);
            empty_groups=1u<<A_STEP_SAND;assert(dos_sound_hit(B_SAND,0,0,0) && last_variant[A_STEP_GRASS]>=0);
            dos_sound_step(B_GRAVEL,200);assert(last_variant[A_STEP_GRAVEL]>=0);
            assert(dos_audio_events==before+3);dos_audio_voices_stop();
            /* Hurt and death cries; death falls back to hurt, then to the caller. */
            assert(dos_sound_mob(MOB_ZOMBIE,0,0,0,0) && last_variant[A_ZOMBIE_HURT]>=0 && last_variant[A_ZOMBIE_DEATH]<0);
            assert(dos_sound_mob(MOB_ZOMBIE,1,0,0,0) && last_variant[A_ZOMBIE_DEATH]>=0);
            empty_groups=1u<<A_CREEPER_DEATH;
            assert(dos_sound_mob(MOB_CREEPER,1,0,0,0) && last_variant[A_CREEPER_HURT]>=0 && last_variant[A_CREEPER_DEATH]<0);
            empty_groups=1u<<A_SHEEP_HURT|1u<<A_SHEEP_DEATH;assert(!dos_sound_mob(MOB_SHEEP,1,0,0,0));
            assert(!dos_sound_mob(MOB_TNT,1,0,0,0));empty_groups=0;dos_audio_voices_stop();
            /* Water and the gravel break. */
            dos_sound_splash(0);dos_sound_splash(-16000);dos_sound_swim();
            assert(last_variant[A_WATER_IN]>=0 && last_variant[A_SWIM]>=0);
            sound_play(DOS_SFX_GRAVEL,256,255,0);assert(last_variant[A_BREAK_GRAVEL]>=0);
            dos_audio_voices_stop();
        } else {
            unsigned int before=dos_audio_events;   /* No synthesized calls or water */
            for(int i=0;i<2400;++i) { dos_sound_mob_tick(MOB_ZOMBIE,0,0,0);dos_sound_splash(-8192);dos_sound_swim(); }
            assert(dos_audio_events==before);
            assert(!dos_sound_hit(B_STONE,0,0,0) && !dos_sound_mob(MOB_PIG,1,0,0,0));   /* Original sounds */
        }
        if(!scenario) {
            int last=-1;
            for(int i=0;i<32;++i) { sound_play(SFX_STONE,256,255,0);assert(last_variant[A_STONE]!=last);last=last_variant[A_STONE]; }
        }
    }
    dos_audio_shutdown();assert(!tick && !clock_owner && !initialized && shut && music_shut==1);
    puts("PASS: sampled/basic/speaker selection, mute, rain restart, material steps, Towns balance, mob calls and cries, hits, water, music hooks, position and cleanup");return 0;
}
