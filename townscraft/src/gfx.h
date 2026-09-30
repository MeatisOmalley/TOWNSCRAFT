/* 2D drawing into the 320x240 frame buffer.

   The frame buffer is a VRAM page: drawing goes straight to the hidden page
   and gfx_present() shows it (page flipping), so there is no copy. */
#ifndef GFX_H
#define GFX_H
#include "common.h"

#define SCR_W 320
#define SCR_H 240
#define FB_PITCH 1024   /* Bytes per frame buffer row (VRAM line) */

extern u8 *g_fb;

void gfx_init(void);
void gfx_clear(u8 *fb,u8 c);
void gfx_present(void);
void gfx_wait_flip(void);
void gfx_sync_pages(void);
void gfx_rect(u8 *fb,int x,int y,int w,int h,u8 c);
void gfx_frame(u8 *fb,int x,int y,int w,int h,u8 c);
void gfx_char(u8 *fb,int x,int y,int ch,u8 c);
void gfx_text(u8 *fb,int x,int y,const char *s,u8 c);
void gfx_text_shadow(u8 *fb,int x,int y,const char *s,u8 c,u8 shadow);
void gfx_text_center(u8 *fb,int y,const char *s,u8 c,u8 shadow);
void gfx_darken(u8 *fb,int x,int y,int w,int h);
extern u8 g_darkenLUT[256];

#endif
