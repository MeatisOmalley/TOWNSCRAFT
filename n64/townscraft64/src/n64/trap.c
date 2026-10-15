/* C port of the Towns textured trapezoid loops (trap.S, x86 assembly).
 *
 * Same arithmetic as trap.S, row by row: span ends rounded from 16.16 edge
 * positions, clipping to [0, maxX], the gradient as delta * (16384/width)
 * from g_recip14, the left-clip prestep, and 16.16 u/v stepping. Texels come
 * from a 16x16 tile in a 256-byte-stride atlas page. WRAP masks u and v to
 * the tile; the other loops keep the coordinates inside the tile, as their
 * callers guarantee. TRANSP skips texel 0; SCALE 2 writes each texel twice.
 * Used by DOScraft's software renderer; the RDP renderer replaces it later. */
#include <stdint.h>
#include "common.h"

/* Layout shared with raster.c (struct Trap). */
typedef struct
{
	u8 *row;
	int stride,rows;
	int lx,ldx,lu,ldu,lv,ldv;
	int rx,rdx,ru,rdu,rv,rdv;
	const u8 *tile;
	int maxX;
	u32 pixels;
} Trap;
_Static_assert(sizeof(Trap)==72,"Trap layout must match raster.c");

extern int g_recip14[];

static inline __attribute__((always_inline))
void trap_rows(Trap *t,int scale,int transp,int wrap)
{
	for(; t->rows>0; --t->rows)
	{
		int xl=(t->lx+0x7FFF)>>16,xr=(t->rx+0x7FFF)>>16,clipped=0;
		if(xr>t->maxX) xr=t->maxX;
		if(xl<0) { xl=0; clipped=1; }
		int w=xr-xl;
		if(w>0)
		{
			t->pixels+=w;
			int width=(t->rx-t->lx+0x8000)>>16;
			if(width<1) width=1;
			if(width>320) width=320;
			int r=g_recip14[width];
			int du=(int)((u32)((t->ru-t->lu)>>8)*(u32)r)>>6;
			int dv=(int)((u32)((t->rv-t->lv)>>8)*(u32)r)>>6;
			int u=t->lu,v=t->lv;
			if(clipped)
			{
				int d=((xl<<16)+0x8000-t->lx)>>8;
				u+=(du>>8)*d;
				v+=(dv>>8)*d;
			}
			u8 *dst=t->row+xl*scale;
			const u8 *tile=t->tile;
			if(!wrap)
			{
				/* Integer parts start inside the tile, as in trap.S. */
				u=(u&0xFFFF)|((u>>16)&15)<<16;
				v=(v&0xFFFF)|((v>>16)&15)<<16;
			}
			/* No wrap: trap.S adds to the address's low two bytes separately
			   (ADC on BL and BH), so u and v wrap within the atlas page. */
			uintptr_t base=(uintptr_t)tile&~(uintptr_t)0xFFFF;
			u32 bl=(uintptr_t)tile&255,bh=((uintptr_t)tile>>8)&255;
			while(w--)
			{
				u8 c=wrap ? tile[(((u32)v>>16)&15)*256+(((u32)u>>16)&15)]
				          : *(const u8 *)(base|((bh+((u32)v>>16))&255)<<8|((bl+((u32)u>>16))&255));
				u+=du; v+=dv;
				if(!transp || c)
				{
					dst[0]=c;
					if(scale==2) dst[1]=c;
				}
				dst+=scale;
			}
		}
		t->lx+=t->ldx; t->lu+=t->ldu; t->lv+=t->ldv;
		t->rx+=t->rdx; t->ru+=t->rdu; t->rv+=t->rdv;
		t->row+=t->stride;
	}
}

void trap_opaque1(Trap *t)  { trap_rows(t,1,0,0); }
void trap_opaque2(Trap *t)  { trap_rows(t,2,0,0); }
void trap_transp1(Trap *t)  { trap_rows(t,1,1,0); }
void trap_transp2(Trap *t)  { trap_rows(t,2,1,0); }
void trap_opaque1w(Trap *t) { trap_rows(t,1,0,1); }
void trap_opaque2w(Trap *t) { trap_rows(t,2,0,1); }
void trap_transp1w(Trap *t) { trap_rows(t,1,1,1); }
void trap_transp2w(Trap *t) { trap_rows(t,2,1,1); }
