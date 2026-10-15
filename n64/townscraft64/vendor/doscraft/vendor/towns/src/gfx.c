#include "gfx.h"
#include "hw.h"
#include "sys.h"

extern const u8 g_font8x8[95][8];
u8 *g_fb;
u8 g_darkenLUT[256];

/* Three display pages in VRAM, 128 KB apart (240 lines of 512 bytes).
   One is on screen, one may be waiting for the vertical sync to be shown
   and the third is drawn into, so drawing never waits for a flip. */
#define PAGE_BYTES 0x20000
static int backPage;        /* Page drawn into (g_fb) */
static int shownPage;       /* Page on screen, as far as is known */
static int presentedPage;   /* Page last passed to the CRTC */
static u32 flipVsync;       /* g_vsyncCount when it was */
static int flipPending;

void gfx_init(void)
{
	int i;
	backPage=1;
	shownPage=presentedPage=0;   /* video_init shows page 0 */
	g_fb=(u8 *)VRAM+PAGE_BYTES;
	for(i=0; i<256; ++i)
	{
		/* Palette is 16 ramps x 16 shades: halve the shade. */
		g_darkenLUT[i]=(i&0xF0)|((i&15)>>1);
	}
}

void gfx_clear(u8 *fb,u8 c)
{
	int y;
	for(y=0; y<SCR_H; ++y)
	{
		memset(fb+y*FB_PITCH,c,SCR_W);
	}
}

/* Show the page just drawn and draw into the page that is neither on
   screen nor just presented.  The display start address (FA0, in 8 byte
   units in 256-color mode) may only take effect at the next vertical sync:
   if one has passed since the previous flip, that page is on screen now.
   FA0 is written before the check, so a sync in between can only make the
   chosen page safer (the page shown before both flips). */
void gfx_present(void)
{
	u32 fa0=backPage*(PAGE_BYTES/8);
	outb(0x440,0x11);   /* FA0 */
	outb(0x442,fa0&0xFF);
	outb(0x443,fa0>>8);
	if(flipPending && g_vsyncCount!=flipVsync)
	{
		shownPage=presentedPage;
	}
	presentedPage=backPage;
	backPage=3-shownPage-presentedPage;
	g_fb=(u8 *)VRAM+backPage*PAGE_BYTES;
	flipVsync=g_vsyncCount;
	flipPending=1;
}

int gfx_back_page(void)
{
	return backPage;
}

/* With three pages the back page is never on screen, so drawing can start
   at once.  Kept for callers that used to wait for the flip. */
void gfx_wait_flip(void)
{
}

/* Wait until the page last presented is on screen: a vertical sync has
   begun since the flip (at most 16.7 ms).  The timeout guards against a
   machine that does not deliver the VSYNC interrupt. */
static void wait_shown(void)
{
	if(flipPending)
	{
		u32 t=g_ticks;
		while(g_vsyncCount==flipVsync && g_ticks-t<3);
		flipPending=0;
		shownPage=presentedPage;
	}
}

/* Copy the page just presented into the other two (for screens that are
   only partly redrawn each frame) */
void gfx_sync_pages(void)
{
	int y,p;
	const u8 *src=(const u8 *)VRAM+presentedPage*PAGE_BYTES;
	wait_shown();
	for(p=0; p<3; ++p)
	{
		u8 *dst=(u8 *)VRAM+p*PAGE_BYTES;
		if(p==presentedPage)
		{
			continue;
		}
		for(y=0; y<SCR_H; ++y)
		{
			memcpy(dst+y*FB_PITCH,src+y*FB_PITCH,SCR_W);
		}
	}
	backPage=(shownPage+1)%3;     /* Any page but the one on screen */
	g_fb=(u8 *)VRAM+backPage*PAGE_BYTES;
}

void gfx_rect(u8 *fb,int x,int y,int w,int h,u8 c)
{
	int j;
	if(x<0){w+=x;x=0;}
	if(y<0){h+=y;y=0;}
	if(x+w>SCR_W){w=SCR_W-x;}
	if(y+h>SCR_H){h=SCR_H-y;}
	if(w<=0 || h<=0)
	{
		return;
	}
	for(j=0; j<h; ++j)
	{
		memset(fb+(y+j)*FB_PITCH+x,c,w);
	}
}

void gfx_frame(u8 *fb,int x,int y,int w,int h,u8 c)
{
	gfx_rect(fb,x,y,w,1,c);
	gfx_rect(fb,x,y+h-1,w,1,c);
	gfx_rect(fb,x,y,1,h,c);
	gfx_rect(fb,x+w-1,y,1,h,c);
}

void gfx_darken(u8 *fb,int x,int y,int w,int h)
{
	int i,j;
	if(x<0){w+=x;x=0;}
	if(y<0){h+=y;y=0;}
	if(x+w>SCR_W){w=SCR_W-x;}
	if(y+h>SCR_H){h=SCR_H-y;}
	for(j=0; j<h; ++j)
	{
		u8 *p=fb+(y+j)*FB_PITCH+x;
		for(i=0; i<w; ++i)
		{
			p[i]=g_darkenLUT[p[i]];
		}
	}
}

void gfx_char(u8 *fb,int x,int y,int ch,u8 c)
{
	int i,j;
	const u8 *g;
	if(ch<0x20 || ch>0x7E)
	{
		ch='?';
	}
	g=g_font8x8[ch-0x20];
	for(j=0; j<8; ++j)
	{
		int yy=y+j;
		u8 bits=g[j];
		if(yy<0 || yy>=SCR_H || 0==bits)
		{
			continue;
		}
		for(i=0; i<8; ++i)
		{
			int xx=x+i;
			if((bits>>i)&1 && 0<=xx && xx<SCR_W)
			{
				fb[yy*FB_PITCH+xx]=c;
			}
		}
	}
}

void gfx_text(u8 *fb,int x,int y,const char *s,u8 c)
{
	while(*s)
	{
		gfx_char(fb,x,y,*s++,c);
		x+=8;
	}
}

void gfx_text_shadow(u8 *fb,int x,int y,const char *s,u8 c,u8 shadow)
{
	gfx_text(fb,x+1,y+1,s,shadow);
	gfx_text(fb,x,y,s,c);
}

void gfx_text_center(u8 *fb,int y,const char *s,u8 c,u8 shadow)
{
	gfx_text_shadow(fb,(SCR_W-strlen(s)*8)/2,y,s,c,shadow);
}
