/* Game-facing frame buffer for the Voodoo build (replaces gfx.c).

   The game still draws its 2D layer (HUD, text, menus, crosshair, rain)
   into three 320x240 8-bit RAM pages; the Voodoo shows 640x480 RGB565.
   Presenting a frame:
   - 3D frame (render_frame ran): the view rows hold R96_KEY except where
     2D was drawn over the 3D view; those pixels are written as 2x2 blocks.
   - 2D frame (title, frozen menus): every row that differs from what that
     Voodoo buffer last received is written.
   The HUD rows below the view are diffed the same way in both cases, per
   Voodoo buffer, so a steady HUD costs nothing. Darkening a 3D frame (menu
   backgrounds, death, sleep) first reads the 3D view back into the page. */
#include "gfx.h"
#include "render.h"
#include "r96.h"
#include "r96_sst1.h"

static u8 pages[3][FB_PITCH*SCR_H];
static int back_page,presented_page;
u8 *g_fb;
u8 g_darkenLUT[256];
extern const u8 g_font8x8[95][8];

/* What each Voodoo buffer shows, as 8-bit page rows */
static u8 shadow[2][SCR_H][SCR_W];
static u8 shadowValid[2][SCR_H];
static int voodooBack;
static u32 lut[256],lutVersion=~0u;

void gfx_init(void)
{
	int i;
	back_page=1;
	presented_page=0;
	g_fb=pages[back_page];
	for(i=0; i<256; ++i)
		g_darkenLUT[i]=(i&0xf0)|((i&15)>>1);
}

/* One page row to Voodoo rows 2y and 2y+1, each pixel doubled. */
static void put_row(int y,const u8 *row)
{
	volatile u32 *a=(volatile u32 *)r96_lfb+(u32)y*R96_LFB_PITCH;
	volatile u32 *b=a+R96_LFB_PITCH/2;
	int x;
	for(x=0; x<SCR_W; ++x) a[x]=lut[row[x]];
	for(x=0; x<SCR_W; ++x) b[x]=lut[row[x]];
}

static void put_rows(int y0,int y1,const u8 *fb)
{
	int y;
	for(y=y0; y<y1; ++y)
	{
		const u8 *row=fb+y*FB_PITCH;
		if(shadowValid[voodooBack][y] && !memcmp(shadow[voodooBack][y],row,SCR_W)) continue;
		put_row(y,row);
		memcpy(shadow[voodooBack][y],row,SCR_W);
		shadowValid[voodooBack][y]=1;
	}
}

/* 2D drawn over the 3D view: everything but the key, in 2x2 blocks. */
static void put_overlays(const u8 *fb)
{
	int y,x;
	for(y=0; y<VIEW_H; ++y)
	{
		const u32 *row=(const u32 *)(fb+y*FB_PITCH);
		volatile u32 *a=(volatile u32 *)r96_lfb+(u32)y*R96_LFB_PITCH;
		for(x=0; x<SCR_W/4; ++x)
		{
			int k;
			if(row[x]==R96_KEY*0x01010101u) continue;
			for(k=0; k<4; ++k)
			{
				u8 c=((const u8 *)row)[x*4+k];
				if(c==R96_KEY) continue;
				a[x*4+k]=lut[c];
				a[x*4+k+R96_LFB_PITCH/2]=lut[c];
			}
		}
		shadowValid[voodooBack][y]=0;
	}
}

void gfx_present(void)
{
	int i;
	if(lutVersion!=r96_palette_version)
	{
		for(i=0; i<256; ++i) lut[i]=r96_rgb565(r96_palette[i])*0x00010001u;
		lutVersion=r96_palette_version;
		memset(shadowValid,0,sizeof(shadowValid));
	}
	if(r96_frame_3d)
	{
		put_overlays(g_fb);
		put_rows(VIEW_H,SCR_H,g_fb);
	}
	else
	{
		put_rows(0,SCR_H,g_fb);
	}
	r96_sst1_swap(0);
	voodooBack^=1;
	r96_frame_3d=0;
	presented_page=back_page;
	back_page=(presented_page+1)%3;
	g_fb=pages[back_page];
}

int gfx_back_page(void)
{
	return back_page;
}

void gfx_wait_flip(void)
{
	/* The Voodoo's own FIFO orders drawing after the swap. */
}

void gfx_sync_pages(void)
{
	int page,y;
	for(page=0; page<3; ++page)
	{
		if(page==presented_page) continue;
		for(y=0; y<SCR_H; ++y)
			memcpy(pages[page]+y*FB_PITCH,pages[presented_page]+y*FB_PITCH,SCR_W);
	}
	back_page=(presented_page+1)%3;
	g_fb=pages[back_page];
}

/* The pinned drawing routines; darkening gets the 3D view first. */
#define gfx_darken r96_towns_gfx_darken
void r96_towns_gfx_darken(u8 *fb,int x,int y,int w,int h);
#include "towns_gfx_draw.inc"
#undef gfx_darken

void gfx_darken(u8 *fb,int x,int y,int w,int h)
{
	if(r96_frame_3d && fb==g_fb)
	{
		r96_3dfx_read_view(fb,FB_PITCH);
		r96_frame_3d=0;
	}
	r96_towns_gfx_darken(fb,x,y,w,h);
}
