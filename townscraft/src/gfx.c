#include "gfx.h"

extern const u8 g_font8x8[95][8];
u8 *g_fb;
u8 g_darkenLUT[256];
static u8 fbStatic[SCR_W*SCR_H];

void gfx_init(void)
{
	int i;
	g_fb=fbStatic;
	for(i=0; i<256; ++i)
	{
		/* Palette is 16 ramps x 16 shades: halve the shade. */
		g_darkenLUT[i]=(i&0xF0)|((i&15)>>1);
	}
}

void gfx_clear(u8 *fb,u8 c)
{
	memset(fb,c,SCR_W*SCR_H);
}

void gfx_present(const u8 *fb)
{
	int y;
	volatile u8 *dst=VRAM;
	for(y=0; y<SCR_H; ++y)
	{
		memcpy((void *)dst,fb,SCR_W);
		dst+=VRAM_PITCH;
		fb+=SCR_W;
	}
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
		memset(fb+(y+j)*SCR_W+x,c,w);
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
		u8 *p=fb+(y+j)*SCR_W+x;
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
				fb[yy*SCR_W+xx]=c;
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
