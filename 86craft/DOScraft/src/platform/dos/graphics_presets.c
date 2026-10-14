#include "graphics_presets.h"
#include "render.h"
#ifdef DOS_FAR_FIELD
#include "far_field.h"
#endif

/* 386 CPU (system.c): only Low and Very Low (Medium/High resolve to Low).
 * Low is 80x100, textured to 4 blocks and flat to 8; Very Low is 80x50 to
 * 4 blocks. */
static const DosGraphicsSettings presets386[DOS_GRAPHICS_COUNT] = {
    {2, 8, 4, 0, SUBDIV_ADAPTIVE},
    {2, 8, 4, 0, SUBDIV_ADAPTIVE},
    {2, 8, 4, 0, SUBDIV_ADAPTIVE},
    {2, 4, 0, 0, SUBDIV_ADAPTIVE}
};
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
    if(dos_cpu386) /* 386: 80x100 and 80x50 only. */
        return dos_resolution_set(dos_resolution_index(scale)==DOS_RES_VERY_LOW ?
            DOS_RES_POTATO : DOS_RES_VERY_LOW);
    return dos_resolution_set((dos_resolution_index(scale)+direction+DOS_RES_COUNT)%DOS_RES_COUNT);
}

void dos_graphics_apply_resolution(int index)
{
    if(dos_cpu386) {
        dos_resolution_set(index==DOS_GRAPHICS_VERY_LOW ? DOS_RES_POTATO : DOS_RES_VERY_LOW);
#ifdef DOS_FAR_FIELD
        /* Both 386 presets try Low distant terrain (map availability permitting). */
        if(dos_far_available()) dos_far_enabled=DOS_FAR_LOW;
#endif
        return;
    }
    dos_very_low_mode=index==DOS_GRAPHICS_VERY_LOW;
    dos_potato_mode=0;
}

const char *dos_resolution_name(int scale)
{
    static const char *const names[]={"320x200","160x100","80x100","80x50 (Potato)"};
    return names[dos_resolution_index(scale)];
}

int dos_graphics_index(int index)
{
    if(dos_cpu386) return index == DOS_GRAPHICS_VERY_LOW ? index : DOS_GRAPHICS_DEFAULT;
    return index >= 0 && index < DOS_GRAPHICS_COUNT ? index : DOS_GRAPHICS_DEFAULT;
}

int dos_graphics_next(int index)
{
    return dos_graphics_step(index, 1);
}

int dos_graphics_step(int index, int direction)
{
    if(index < 0 || index >= DOS_GRAPHICS_COUNT) return DOS_GRAPHICS_DEFAULT;
    if(dos_cpu386) return index == DOS_GRAPHICS_VERY_LOW ? DOS_GRAPHICS_LOW : DOS_GRAPHICS_VERY_LOW;
    return (index + direction + DOS_GRAPHICS_COUNT) % DOS_GRAPHICS_COUNT;
}

const DosGraphicsSettings *dos_graphics_settings(int index)
{
    return &(dos_cpu386 ? presets386 : presets)[dos_graphics_index(index)];
}

const char *dos_graphics_message(int index)
{
    return messages[dos_graphics_index(index)];
}
