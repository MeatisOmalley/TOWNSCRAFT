/* Bounded DOS graphics policy. Pure queries; no renderer or I/O writes. */
#ifndef DOSCRAFT_GRAPHICS_PRESETS_H
#define DOSCRAFT_GRAPHICS_PRESETS_H

enum {
    DOS_GRAPHICS_LOW,
    DOS_GRAPHICS_MEDIUM,
    DOS_GRAPHICS_HIGH,
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

/* Invalid indices resolve to Low. Invalid next indices also return Low. */
int dos_graphics_index(int index);
int dos_graphics_next(int index);
const DosGraphicsSettings *dos_graphics_settings(int index);
const char *dos_graphics_message(int index);

#endif
