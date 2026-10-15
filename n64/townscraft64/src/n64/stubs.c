/* Temporary stand-ins while N64 audio is written: DOScraft's audio events
 * do nothing, like its silent backend (sound_silent.c). */
void dos_sound_step(unsigned char block, int vol) { (void)block; (void)vol; }
int dos_sound_hit(unsigned char block, int x, int y, int z) { (void)block; (void)x; (void)y; (void)z; return 0; }
int dos_sound_mob(int type, int death, int x, int y, int z)
{ (void)type; (void)death; (void)x; (void)y; (void)z; return 0; }
void dos_sound_mob_tick(int type, int x, int y, int z) { (void)type; (void)x; (void)y; (void)z; }
void dos_sound_splash(int vy) { (void)vy; }
void dos_sound_swim(void) {}
