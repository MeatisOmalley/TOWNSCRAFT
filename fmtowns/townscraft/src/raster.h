#ifndef RASTER_H
#define RASTER_H
#include "common.h"

#define SCR_MAX_W 320
extern u32 g_statPixels;

typedef struct
{
	int x,y;   /* 28.4 screen */
	int u,v;   /* 16.16 texels */
} RVert;

void raster_set_target(u8 *buf,int w,int h,int pitch,int scale);
int raster_width(void);
int raster_height(void);
/* raster_poly flags */
#define RP_TRANSPARENT 1   /* texel 0 is not drawn */
#define RP_WRAP        2   /* texture coordinates repeat (may leave 0..16) */
extern int g_fastFlat;     /* Inline span stores for the distance presets */
void raster_poly(const RVert *v,int n,const u8 *tex,int flags);
void raster_flat_poly(const RVert *v,int n,u8 color);
void raster_pixel(int x,int y,u8 color);
void raster_line(int x0,int y0,int x1,int y1,u8 color);
void raster_fill_rows(int y0,int y1,u8 color);
void raster_finish(void);

/* A tile is valid only when one opaque polygon covers every pixel in it.
   Depth and painter order must both prove that its occluder draws in front. */
extern u32 g_statOcclusionTiles;
void raster_occlusion_reset(void);
void raster_occluder(const RVert *v,int n,int farDepth,u32 key);
int raster_occluded(int minx,int maxx,int miny,int maxy,int nearDepth,u32 key);

#endif
