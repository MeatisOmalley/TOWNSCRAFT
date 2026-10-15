/* Temporary stand-ins while the N64 save store and audio are written.
 * Saves report "no disk" (the game says so and keeps running); DOScraft's
 * audio events do nothing, like its silent backend (sound_silent.c). */
#include "save.h"

int save_world(void) { return SAVE_NO_DISK; }
int load_world(void) { return SAVE_NO_DISK; }
int save_loaded_mobs(void) { return 0; }
void save_stream_tick(void) {}
int dos_save_has_world(void) { return 0; }
int dos_save_clear_world(void) { return SAVE_OK; }

void dos_sound_step(unsigned char block, int vol) { (void)block; (void)vol; }
int dos_sound_hit(unsigned char block, int x, int y, int z) { (void)block; (void)x; (void)y; (void)z; return 0; }
int dos_sound_mob(int type, int death, int x, int y, int z)
{ (void)type; (void)death; (void)x; (void)y; (void)z; return 0; }
void dos_sound_mob_tick(int type, int x, int y, int z) { (void)type; (void)x; (void)y; (void)z; }
void dos_sound_splash(int vy) { (void)vy; }
void dos_sound_swim(void) {}
