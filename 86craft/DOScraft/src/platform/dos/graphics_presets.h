/* Bounded DOS graphics policy. Pure queries; no renderer or I/O writes. */
#ifndef DOSCRAFT_GRAPHICS_PRESETS_H
#define DOSCRAFT_GRAPHICS_PRESETS_H

enum {
    DOS_GRAPHICS_LOW,
    DOS_GRAPHICS_MEDIUM,
    DOS_GRAPHICS_HIGH,
    DOS_GRAPHICS_VERY_LOW,
    DOS_GRAPHICS_COUNT,
#ifdef DOS_R96
    /* 1996 build: a Pentium 200 starts at the top preset. */
    DOS_GRAPHICS_DEFAULT = DOS_GRAPHICS_HIGH
#else
    DOS_GRAPHICS_DEFAULT = DOS_GRAPHICS_LOW
#endif
};

typedef struct {
    int render_scale;
    int view_distance;
    int flat_distance; /* -1: textureless; 0: all textured; positive: cutoff. */
    int distance_mode;
    int texture_subdivision; /* Imported render.h SUBDIV_* value. */
} DosGraphicsSettings;

extern int dos_very_low_mode;
extern int dos_potato_mode;
enum { DOS_RES_FULL,DOS_RES_LOW,DOS_RES_VERY_LOW,DOS_RES_POTATO,DOS_RES_COUNT };
int dos_resolution_index(int scale);
int dos_resolution_set(int index);
int dos_resolution_change(int scale,int direction);
const char *dos_resolution_name(int scale);
/* Sets dos_very_low_mode/dos_potato_mode for a preset's resolution. */
void dos_graphics_apply_resolution(int index);
/* Set at startup when the CPU is a 386 (system.c): the 386 keeps to the Low
 * and Very Low presets and 8 blocks. */
extern int dos_cpu386;
#define DOS_GRAPHICS_MAX_DISTANCE (dos_cpu386 ? 8 : 16)

/* Preserve the renderer's historical zero (all textured) representation. */
static inline int dos_textured_distance(int flat, int view)
{
    return flat<0 ? 0 : flat ? flat : view;
}
static inline int dos_flat_cutoff(int textured, int view)
{
    return textured==0 ? -1 : textured<view ? textured : 0;
}

/* Invalid indices resolve to Low. Invalid next indices also return Low. */
int dos_graphics_index(int index);
int dos_graphics_next(int index);
int dos_graphics_step(int index, int direction);
const DosGraphicsSettings *dos_graphics_settings(int index);
const char *dos_graphics_message(int index);

#endif
