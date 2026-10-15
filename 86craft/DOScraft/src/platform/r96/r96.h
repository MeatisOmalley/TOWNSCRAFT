/* 1996 build (Pentium 200, optional 3dfx Voodoo Graphics): shared backend
   declarations. See HARDWARE_1996.md. */
#ifndef DOSCRAFT_R96_H
#define DOSCRAFT_R96_H
#include "common.h"

/* Software backend: nonzero draws perspective spans with the C reference
   instead of r96_span.S (tests and diagnostics). */
extern int r96_reference_spans;

/* The atlas or texture metadata changed (render.c, R96 overlay). */
void r96_textures_changed(void);

/* ---- Voodoo build ---- */

/* 3D frames leave this index in the 8-bit page's view rows; any other
   value there was drawn over the 3D view (crosshair, text, rain) and is
   composited onto the Voodoo frame. Olive level 0 is never drawn by the UI
   (the hand's peach and the rain's sky ramps reach the Voodoo or differ). */
#define R96_KEY 0xF0
/* Display palette as 0xRRGGBB, from video_set_palette. */
extern u32 r96_palette[256];
extern int r96_palette_dirty;
extern u32 r96_palette_version;   /* counts palette changes */
/* The current 8-bit page's view rows hold R96_KEY over a Voodoo frame. */
extern int r96_frame_3d;
/* RGB565 of a palette index. */
static inline u16 r96_rgb565(u32 rgb)
{
	return (u16)(((rgb>>8)&0xF800)|((rgb>>5)&0x07E0)|((rgb>>3)&0x001F));
}
/* Voodoo pixels per logical pixel of the current 3D view. */
extern int r96_scale_x,r96_scale_y;
/* Back buffer to an 8-bit page's view rows where they hold R96_KEY (for
   effects that read the 3D view, such as darkening menus). */
void r96_3dfx_read_view(u8 *page,int pitch);

#endif
