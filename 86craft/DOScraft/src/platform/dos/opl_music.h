#ifndef DOS_OPL_MUSIC_H
#define DOS_OPL_MUSIC_H
/* Music on an AdLib/Sound Blaster OPL2 (port 388h): the original Towns piece
 * plus any songs in C:\MUSIC.PAK or A:\MUSIC.PAK. Startup after
 * dos_sound_assets_build and the game arenas; the 100 Hz timer hook calls
 * dos_music_tick. */
int dos_music_init(void);
void dos_music_tick(void);
void dos_music_shutdown(void);
const char *dos_music_backend(void);
/* Songs loaded from MUSIC.PAK (0 without a pack). */
unsigned int dos_music_songs(void);
/* The piece playing or last played: a pack song index, or -1 for the Towns piece. */
int dos_music_current(void);
extern unsigned int dos_music_notes;
#endif
