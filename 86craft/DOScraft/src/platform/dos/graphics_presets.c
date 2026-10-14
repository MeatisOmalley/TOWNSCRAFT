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
int dos_potato_mode;

int dos_resolution_index(int scale)
{
    return scale==1 ? DOS_RES_FULL : dos_potato_mode ? DOS_RES_POTATO :
        dos_very_low_mode ? DOS_RES_VERY_LOW : DOS_RES_LOW;
}

int dos_resolution_set(int index)
{
    if(index<0 || index>=DOS_RES_COUNT) index=DOS_RES_LOW;
    dos_very_low_mode=index>=DOS_RES_VERY_LOW;
    dos_potato_mode=index==DOS_RES_POTATO;
    return index==DOS_RES_FULL ? 1 : 2;
}

int dos_resolution_change(int scale,int direction)
{
    return dos_resolution_set((dos_resolution_index(scale)+direction+DOS_RES_COUNT)%DOS_RES_COUNT);
}

const char *dos_resolution_name(int scale)
{
    static const char *const names[]={"320x200","160x100","80x100","80x50 (Potato)"};
    return names[dos_resolution_index(scale)];
}

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
