#ifndef DOSCRAFT_SAVE_BACKEND_H
#define DOSCRAFT_SAVE_BACKEND_H
#define DOS_SAVE_PATH "C:\\WORLD.SAV"
void dos_save_shutdown(void);
/* The raw fixed-size medium is retained; both published save banks are cleared. */
int dos_save_has_world(void);
int dos_save_clear_world(void);
void dos_game_exit(void);
#endif
