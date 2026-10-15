#ifndef DOS_OPL_MUSIC_H
#define DOS_OPL_MUSIC_H
/* The original score on an AdLib/Sound Blaster OPL2 (port 388h). Startup after
 * dos_sound_assets_build; the 100 Hz timer hook calls dos_music_tick. */
int dos_music_init(void);
void dos_music_tick(void);
void dos_music_shutdown(void);
const char *dos_music_backend(void);
extern unsigned int dos_music_notes;
#endif
