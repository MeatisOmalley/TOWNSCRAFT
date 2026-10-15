/* Shared game effects. Disk I/O/synthesis happen only before playback starts;
 * the locked SB IRQ mixes short blocks even during long frames/DOS disk calls. */
#include "sound.h"
#include "blocks.h"
#include "fmath.h"
#include "audio_bank.h"
#include "audio_voices.h"
#include "game_audio.h"
#include "sound_assets.h"
#include "sb_pcm.h"
#include "keyboard.h"
#include "irq.h"
#include "video_backend.h"
#include <pc.h>
#include <stdlib.h>

int g_musicOn=1,g_sfxOn=1;
unsigned int dos_audio_events;
static int backend,bank_ready,initialized,lis_x,lis_y,lis_z,lis_yaw;
static unsigned int random_state=0x74a39b21u;
static int last_variant[A_GROUPS];
static int speaker_ticks,speaker_priority;
static int loop_started;
static unsigned char speaker_original;
static unsigned int next_random(void)
{ random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
const char *dos_audio_backend(void)
{ return backend==2 ? "SB_SAMPLED" : backend==1 ? "SB_BASIC" : backend==3 ? "PC_SPEAKER" : "OFF"; }
static void mix(unsigned char *out,unsigned int frames) { dos_audio_voices_render(out,frames); }
static void speaker(int id,int priority)
{
    if(speaker_ticks && speaker_priority>priority)return;
    unsigned int divisor=1193182u/(180u+(unsigned)(id%12)*95u);
    outportb(0x43,0xb6);outportb(0x42,divisor&255);outportb(0x42,divisor>>8);
    outportb(0x61,inportb(0x61)|3);speaker_ticks=priority>=4 ? 10 : 4;speaker_priority=priority;
}
static int group_for(int id,int pitch)
{
    switch(id) {
    case SFX_CRUNCH:return A_CRUNCH;
    case SFX_STONE:return A_STONE;
    case SFX_WOOD:return A_WOOD;
    case SFX_STEP:return pitch>=280 ? A_STEP_STONE : pitch>=235 ? A_STEP_WOOD : A_STEP_GRASS;
    case SFX_PLACE:return A_PLACE;
    case SFX_HURT:return A_HURT;
    case SFX_CLICK:return A_CLICK;
    case DOS_SFX_GLASS:return A_GLASS;
    case DOS_SFX_SAND:return A_SAND;
    case DOS_SFX_GRAVEL:return A_STONE;
    default:return -1;
    }
}
static void play_group(int id,int group,int pitch,int vol,int pan,int priority)
{
    if(!initialized || !g_sfxOn || vol<=0)return;
    if(vol>255)vol=255;
    if(pan<-15)pan=-15;
    if(pan>15)pan=15;
    /* Original positional mono fold: off-axis sounds retain half their gain. */
    vol=vol*(30-abs(pan))/30;
    if(backend==3) {
        if(vol>=35 && id!=SFX_RAIN) {
            unsigned long state=dos_keyboard_irq_save();speaker(id,priority);dos_keyboard_irq_restore(state);
            ++dos_audio_events;
        }
        return;
    }
    if(backend!=1 && backend!=2)return;
    DosAudioClip legacy;const DosAudioClip *clip=0;
    unsigned int n=group>=0 && bank_ready ? dos_audio_bank_count(group) : 0;
    if(n) {
        unsigned int variant=next_random()%n;
        if(n>1 && variant==(unsigned)last_variant[group])variant=(variant+1)%n;
        last_variant[group]=(int)variant;clip=dos_audio_bank_clip(group,variant);
        pitch=pitch*(244+(int)(next_random()%25))/256;
    } else {
        if(id<0 || id>=NUM_SFX)id=SFX_CRUNCH;
        const DosSoundSample *s=dos_sound_sample(id);
        const unsigned char *wave=dos_sound_wave(0);
        if(!s || !wave)return;
        legacy=(DosAudioClip){wave+s->offset,s->frames,20833u*s->pitch/256u};clip=&legacy;
        vol=vol*s->volume/256;
    }
    /* Seven simultaneous one-shots plus rain; reserve headroom for overlap. */
    vol=vol*3/5;
    unsigned long state=dos_keyboard_irq_save();
    if(!dos_audio_voice(clip,pitch,vol,priority,0))++dos_audio_events;
    dos_keyboard_irq_restore(state);
}
void sound_play(int id,int pitch,int vol,int pan)
{
    if((id<0 || id>=NUM_SFX) && (id<DOS_SFX_GLASS || id>DOS_SFX_GRAVEL))return;
    int group=group_for(id,pitch),priority=id==SFX_STEP ? 1 : id==SFX_HURT || id==SFX_EXPLODE ? 5 : 3;
    if(id==SFX_STEP && bank_ready)pitch=256;
    play_group(id,group,pitch,vol,pan,priority);
}
void dos_sound_step(unsigned char block,int vol)
{
    int group=A_STEP_GRASS;
    if(block==B_SAND)group=A_SAND;
    else if(g_blockDef[BLK_ID(block)].tool==TOOL_PICK)group=A_STEP_STONE;
    else if(g_blockDef[BLK_ID(block)].tool==TOOL_AXE)group=A_STEP_WOOD;
    play_group(SFX_STEP,group,256,vol,0,1);
}
void sound_set_listener(int x,int y,int z,int yaw)
{ lis_x=x;lis_y=y;lis_z=z;lis_yaw=yaw; }
void sound_play_at(int id,int pitch,int vol,int x,int y,int z)
{
    int dx=(x-lis_x)>>8,dy=(y-lis_y)>>8,dz=(z-lis_z)>>8;
    int d=isqrt((u32)(dx*dx+dy*dy+dz*dz)),pan=0;
    if(d>=256)return;
    vol=vol*(256-d)/256;
    if(d>8)pan=(((dx*fcos(lis_yaw)-dz*fsin(lis_yaw))>>14)*12)/d;
    sound_play(id,pitch,vol,pan);
}
void sound_loop(int id,int vol)
{
    if(id!=SFX_RAIN || backend<1 || backend>2)return;
    if(!g_sfxOn)vol=0;
    if(vol<0)vol=0;
    if(vol>255)vol=255;
    unsigned long state=dos_keyboard_irq_save();
    if(!vol) { dos_audio_loop_volume(0);loop_started=0; }
    else if(!loop_started) {
        const DosSoundSample *s=dos_sound_sample(id);const unsigned char *wave=dos_sound_wave(0);
        if(s && wave) {
            DosAudioClip clip={wave+s->offset,s->frames,20833u*s->pitch/256u};
            if(!dos_audio_voice(&clip,256,vol*s->volume/512,0,1))loop_started=1;
        }
    } else {
        const DosSoundSample *s=dos_sound_sample(id);dos_audio_loop_volume(vol*s->volume/512);
    }
    dos_keyboard_irq_restore(state);
}
void sound_tick(void)
{
    if(backend==3 && speaker_ticks && (!g_sfxOn || !--speaker_ticks)) {
        outportb(0x61,inportb(0x61)&~3);speaker_ticks=0;
    }
    if((backend==1 || backend==2) && !g_sfxOn) { dos_audio_voices_stop();loop_started=0; }
}
void dos_audio_shutdown(void)
{
    if(!initialized)return;
    dos_irq_set_tick_hook(0);dos_sb_shutdown();
    if(backend==3) {
        outportb(0x61,speaker_original);dos_video_set_speaker_clock(0);
    }
    dos_audio_voices_stop();backend=initialized=speaker_ticks=loop_started=0;
}
void sound_init(void)
{
    if(initialized)return;
    for(int i=0;i<A_GROUPS;++i)last_variant[i]=-1;
    dos_audio_events=0;
    bank_ready=dos_audio_bank_load("C:\\SOUND.PAK")==0;
    if(!bank_ready)bank_ready=dos_audio_bank_load("A:\\SOUND.PAK")==0;
    int basic=dos_sound_assets_build()==0;
    dos_audio_voices_reset(DOS_SB_RATE);
    if((bank_ready || basic) && !dos_sb_init()) {
        if(!dos_sb_stream_start(mix))backend=bank_ready ? 2 : 1;
        else dos_sb_shutdown();
    }
    if(!backend) {
        dos_video_set_speaker_clock(1);speaker_original=inportb(0x61);backend=3;
    }
    initialized=1;
    if(atexit(dos_audio_shutdown)) { dos_audio_shutdown();return; }
    dos_irq_set_tick_hook(sound_tick);
}
/* Music remains separate from this effects implementation. */
void music_schedule(int ticks) { (void)ticks; }
void music_set_gap(int ticks) { (void)ticks; }
void music_stop(void) {}
