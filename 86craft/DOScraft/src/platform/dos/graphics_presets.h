/* Bounded DOS graphics policy. Pure queries; no renderer or I/O writes. */
#ifndef DOSCRAFT_GRAPHICS_PRESETS_H
#define DOSCRAFT_GRAPHICS_PRESETS_H

enum {
    DOS_GRAPHICS_LOW,
    DOS_GRAPHICS_MEDIUM,
    DOS_GRAPHICS_HIGH,
    DOS_GRAPHICS_VERY_LOW,
    DOS_GRAPHICS_COUNT,
    DOS_GRAPHICS_DEFAULT = DOS_GRAPHICS_LOW
};

typedef struct {
    int render_scale;
    int view_distance;
    int flat_distance; /* 0: all distances textured; otherwise start here. */
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

/* Invalid indices resolve to Low. Invalid next indices also return Low. */
int dos_graphics_index(int index);
int dos_graphics_next(int index);
const DosGraphicsSettings *dos_graphics_settings(int index);
const char *dos_graphics_message(int index);

#endif
