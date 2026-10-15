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
extern unsigned int dos_audio_voice_drops,dos_audio_mixed_frames;
#endif
