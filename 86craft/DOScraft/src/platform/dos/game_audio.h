#ifndef DOS_GAME_AUDIO_H
#define DOS_GAME_AUDIO_H
enum { DOS_SFX_GLASS=100,DOS_SFX_SAND,DOS_SFX_GRAVEL };
void dos_sound_step(unsigned char block,int volume);
void dos_audio_shutdown(void);
const char *dos_audio_backend(void);
extern unsigned int dos_audio_events;
#endif
