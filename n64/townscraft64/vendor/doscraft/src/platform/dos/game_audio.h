#ifndef DOS_GAME_AUDIO_H
#define DOS_GAME_AUDIO_H
/* Sampled-only events (from DOS_SFX_GROAN on) have no synthesized fallback. */
enum { DOS_SFX_GLASS=100,DOS_SFX_SAND,DOS_SFX_GRAVEL,DOS_SFX_GROAN,DOS_SFX_WATER };
void dos_sound_step(unsigned char block,int volume);
/* A mining swing, as in Minecraft: the block's footstep, quiet and an octave
 * down. Returns 0 when the bank has no such step, so the caller plays the
 * original hit sound. */
int dos_sound_hit(unsigned char block,int x,int y,int z);
/* A mob's hurt or death cry (death falls back to hurt). Returns 0 when the
 * bank has neither, so the caller plays the original pitched hurt sound. */
int dos_sound_mob(int type,int death,int x,int y,int z);
/* Called for each mob on every 20 Hz mob tick: idle calls now and then
 * (the zombie's is its groan). Its own random source; no game state. */
void dos_sound_mob_tick(int type,int x,int y,int z);
/* The player enters water (vy: vertical speed then), or swims a stroke. */
void dos_sound_splash(int vy);
void dos_sound_swim(void);
void dos_audio_shutdown(void);
const char *dos_audio_backend(void);
extern unsigned int dos_audio_events;
#endif
