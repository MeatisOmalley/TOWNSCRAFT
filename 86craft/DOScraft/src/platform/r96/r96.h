/* 1996 build (Pentium 200, optional 3dfx Voodoo Graphics): shared backend
   declarations. See HARDWARE_1996.md. */
#ifndef DOSCRAFT_R96_H
#define DOSCRAFT_R96_H
#include "common.h"

/* Software backend: nonzero draws perspective spans with the C reference
   instead of r96_span.S (tests and diagnostics). */
extern int r96_reference_spans;

/* Workload counters of the software backend (R96_STATS builds only). */
typedef struct
{
	u32 persp,affine,flat,pixel;    /* raster calls by kind */
	u32 rows,spans,pixels,flatPixels;
	u32 segments;                   /* 16-pixel perspective segments */
	u32 spanLen[6];                 /* 1,2-3,4-7,8-15,16-31,32+ */
	u32 depthRatio[6];              /* (qmax-qmin)/qmin: <1%,<2%,<5%,<10%,<25%,more */
	u32 affinePolys,affineRows,affinePixels; /* depth ratio x texel range <= 1/2 */
} R96Stats;
extern R96Stats r96_stats;

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
/* Page bookkeeping for re-keying only what 2D drawing touched: a view row
   of the page held an overlay, or the page was used for a 2D frame. */
void r96_3dfx_page_dirty_row(u8 *page,int y);
void r96_3dfx_page_unkeyed(u8 *page);

#endif
