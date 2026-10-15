#ifndef DOS_GAME_AUDIO_H
#define DOS_GAME_AUDIO_H
enum { DOS_SFX_GLASS=100,DOS_SFX_SAND,DOS_SFX_GRAVEL,DOS_SFX_GROAN };
void dos_sound_step(unsigned char block,int volume);
/* Called for each zombie on every 20 Hz mob tick; groans now and then. */
void dos_sound_zombie(int x,int y,int z);
void dos_audio_shutdown(void);
const char *dos_audio_backend(void);
extern unsigned int dos_audio_events;
#endif
