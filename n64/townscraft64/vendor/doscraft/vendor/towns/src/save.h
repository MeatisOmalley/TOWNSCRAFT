/* Saving and loading worlds on a floppy disk (drive A, 1232 KB). */
#ifndef SAVE_H
#define SAVE_H
#include "common.h"

enum
{
	SAVE_OK,
	SAVE_NO_DISK,
	SAVE_DISK_ERROR,
	SAVE_NOT_A_SAVE,
	SAVE_WRONG_SIZE,
	SAVE_BAD_DATA
};

/* Both show progress through g_genProgress (world.h) if set.  After a
   successful load the caller rebuilds light and meshes
   (world_rebuild_after_load). */
int save_world(void);
int load_world(void);
int save_loaded_mobs(void);
void save_stream_tick(void);
const char *save_error_text(int err);

#endif
