#ifndef DOS_AUDIO_VOICES_H
#define DOS_AUDIO_VOICES_H
#include "audio_bank.h"
/* Serialized foreground/IRQ calls only; callers own a short interrupt guard.
 * Render is bounded and integer-only. Sample storage must remain locked. */
void dos_audio_voices_reset(unsigned int rate);
int dos_audio_voice(const DosAudioClip *clip,int pitch,int volume,int priority,int loop);
void dos_audio_loop_volume(int volume);
void dos_audio_voices_stop(void);
void dos_audio_voices_render(unsigned char *output,unsigned int frames);
/* Mixed sums span +-8 voices x 127; output is exact up to the knee. */
#define DOS_AUDIO_MIX_LIMIT 1024
#define DOS_AUDIO_KNEE 96
int dos_audio_soft_clip(int value);
extern unsigned int dos_audio_voice_drops,dos_audio_mixed_frames;
#endif
