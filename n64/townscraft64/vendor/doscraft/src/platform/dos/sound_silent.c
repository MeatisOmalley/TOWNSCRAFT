/* Temporary silent port backend. User explicitly deferred audio redesign in
 * favor of a functioning game. Keep source setting flags/API; no SB IRQ/mixer.
 * This is not music/effect parity and is recorded in the release manifest. */
#include "sound.h"
int g_musicOn=1,g_sfxOn=1;
void sound_init(void) {}
void sound_play(int id,int pitch,int vol,int pan)
{ (void)id; (void)pitch; (void)vol; (void)pan; }
void sound_loop(int id,int vol) { (void)id; (void)vol; }
void sound_set_listener(int x,int y,int z,int yaw)
{ (void)x; (void)y; (void)z; (void)yaw; }
void sound_play_at(int id,int pitch,int vol,int x,int y,int z)
{ (void)id; (void)pitch; (void)vol; (void)x; (void)y; (void)z; }
void music_schedule(int ticks) { (void)ticks; }
void music_set_gap(int ticks) { (void)ticks; }
void music_stop(void) {}
void sound_tick(void) {}
