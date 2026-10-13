#include "graphics_presets.h"
#include "render.h"

static const DosGraphicsSettings presets[DOS_GRAPHICS_COUNT] = {
    {2, 8, 0, 0, SUBDIV_ADAPTIVE},
    {2, 8, 0, 0, SUBDIV_ADAPTIVE},
    {1, 8, 0, 0, SUBDIV_ADAPTIVE},
    {2, 8, 0, 0, SUBDIV_ADAPTIVE}
};

static const char *const messages[DOS_GRAPHICS_COUNT] = {
    "Graphics: Low", "Graphics: Medium", "Graphics: High", "Graphics: Very Low"
};

int dos_very_low_mode;

int dos_graphics_index(int index)
{
    return index >= 0 && index < DOS_GRAPHICS_COUNT ? index : DOS_GRAPHICS_DEFAULT;
}

int dos_graphics_next(int index)
{
    if(index < 0 || index >= DOS_GRAPHICS_COUNT) return DOS_GRAPHICS_DEFAULT;
    return (index + 1) % DOS_GRAPHICS_COUNT;
}

const DosGraphicsSettings *dos_graphics_settings(int index)
{
    return &presets[dos_graphics_index(index)];
}

const char *dos_graphics_message(int index)
{
    return messages[dos_graphics_index(index)];
}
