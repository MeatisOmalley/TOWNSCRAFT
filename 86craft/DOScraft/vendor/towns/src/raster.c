/* Convex polygon rasterizer.

   Screen coordinates are 28.4 fixed point.  Texture coordinates are 16.16
   texels, interpolated along the polygon edges and then linearly across each
   span, so every edge maps exactly.  Textures repeat every 16 texels (merged
   quads cover several blocks; RP_WRAP selects the loops that mask the
   coordinates, the others require them to stay inside one tile).  Textured polygons are split into
   trapezoids (rows between vertex events), each drawn by one call to the
   assembly loops in trap.S.

   In scale 2 mode each render pixel is written as two bytes into the frame
   buffer, and raster_finish() copies even rows to odd rows. */
#include "raster.h"
#include "fmath.h"
#include "gfx.h"
#include "bench.h"

/* Layout shared with trap.S */
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

void trap_opaque1(Trap *t);
void trap_opaque2(Trap *t);
void trap_transp1(Trap *t);
void trap_transp2(Trap *t);
void trap_opaque1w(Trap *t);
void trap_opaque2w(Trap *t);
void trap_transp1w(Trap *t);
void trap_transp2w(Trap *t);

/* Indexed by (scale-1) | flags<<1 */
static void (*const trapFunc[8])(Trap *)=
{
	trap_opaque1,trap_opaque2,trap_transp1,trap_transp2,
	trap_opaque1w,trap_opaque2w,trap_transp1w,trap_transp2w,
};

static u8 *rbuf;
static int rw,rh,rpitch,rscale;
int g_recip14[SCR_MAX_W+1];   /* 16384/n, used by trap.S */
u32 g_statPixels;
int g_fastFlat;

#define OCC_COLS (SCR_W/16)
#define OCC_ROWS ((VIEW_H+15)/16)
static u16 occDepth[OCC_COLS*OCC_ROWS];
static u32 occKey[OCC_COLS*OCC_ROWS];
static int occShift;
u32 g_statOcclusionTiles;

void raster_occlusion_reset(void)
{
	occShift=(rscale==2) ? 3 : 4;
	memset(occDepth,255,sizeof(occDepth));
	g_statOcclusionTiles=0;
}

int raster_occluded(int minx,int maxx,int miny,int maxy,int nearDepth,u32 key)
{
	int x0,x1,y0,y1,x,y;
	if(!g_statOcclusionTiles || nearDepth<=0) return 0;
	/* Enclose pixel coverage, including the tiny-face point shortcut. */
	x0=MAX(0,(minx-16)>>4)>>occShift;
	x1=MIN(rw-1,(maxx+16)>>4)>>occShift;
	y0=MAX(0,(miny-16)>>4)>>occShift;
	y1=MIN(rh-1,(maxy+16)>>4)>>occShift;
	for(y=y0; y<=y1; ++y) for(x=x0; x<=x1; ++x)
	{
		int i=y*OCC_COLS+x;
		if(occDepth[i]==65535 || nearDepth<=occDepth[i]+8 || key<=occKey[i]) return 0;
	}
	return 1;
}

void raster_set_target(u8 *buf,int w,int h,int pitch,int scale)
{
	int i;
	rbuf=buf;
	rw=w;
	rh=h;
	rpitch=pitch;
	rscale=scale;
	if(0==g_recip14[1])
	{
		for(i=1; i<=SCR_MAX_W; ++i)
		{
			g_recip14[i]=16384/i;
		}
	}
}

int raster_width(void){return rw;}
int raster_height(void){return rh;}

static inline int ceil4(int v)
{
	/* First pixel/scanline whose center (n*16+8) is >= v */
	return (v+7)>>4;
}

typedef struct
{
	int x16,dx,u,du,v,dv;
	int cur,end;       /* vertex indices */
	int sEnd;          /* first scanline after this edge */
} PEdge;

/* Set up edge from vertex a to b starting at scanline s */
static inline void pedge_init(PEdge *e,const RVert *a,const RVert *b,int s,int tex)
{
	int dy=b->y-a->y,pre=s*16+8-a->y;
	e->dx=divshift(b->x-a->x,dy,16);
	e->x16=(a->x<<12)+mulshift(e->dx,pre,4);
	if(tex)
	{
		/* On block faces each edge changes only u or only v */
		int du=b->u-a->u,dv=b->v-a->v;
		if(du)
		{
			e->du=divshift(du,dy,4);
			e->u=a->u+mulshift(e->du,pre,4);
		}
		else
		{
			e->du=0;
			e->u=a->u;
		}
		if(dv)
		{
			e->dv=divshift(dv,dy,4);
			e->v=a->v+mulshift(e->dv,pre,4);
		}
		else
		{
			e->dv=0;
			e->v=a->v;
		}
	}
}

/* Advance a chain to the edge covering scanline s.  dir is +1 or -1.
   Returns 0 when the chain is exhausted. */
static inline int pedge_next(PEdge *e,const RVert *v,int n,int dir,int s,int tex)
{
	int guard=n;
	while(s>=e->sEnd)
	{
		if(--guard<0)
		{
			return 0;
		}
		int a=e->end,b=a+dir;
		if(b<0) b+=n;
		if(b>=n) b-=n;
		if(v[b].y<v[a].y)
		{
			return 0;  /* Reached the bottom */
		}
		e->cur=a;
		e->end=b;
		e->sEnd=ceil4(v[b].y);
		if(s<e->sEnd)
		{
			pedge_init(e,&v[a],&v[b],s,tex);
		}
	}
	return 1;
}

/* Reuse the repeated color instead of rebuilding it in memset on each row.
   Alignment keeps the word stores suitable for the original 386 target. */
static inline void flat_span(u8 *dst,int n,u32 color)
{
	int prefix=MIN((-(u32)dst)&3,(u32)n),tail,words;
	n-=prefix;
	while(prefix--) *dst++=(u8)color;
	tail=n&3; words=n>>2;
	__asm__ volatile("rep stosl" : "+D"(dst),"+c"(words) : "a"(color) : "memory");
	while(tail--) *dst++=(u8)color;
}

static void poly_scan(const RVert *v,int n,const u8 *tile,int flags,u8 flat)
{
	int i,top=0,bot=0,area=0,s,sTop,sBot;
	int tex=(NULL!=tile);
	u32 flatWord=(u32)flat*0x01010101u;
	PEdge eL,eR,*left,*right;
	for(i=1; i<n; ++i)
	{
		if(v[i].y<v[top].y) top=i;
		if(v[i].y>v[bot].y) bot=i;
	}
	/* Orientation of a convex polygon: first non-zero corner cross product */
	for(i=0; i+2<n && 0==area; ++i)
	{
		area=((v[i+1].x-v[i].x)>>2)*((v[i+2].y-v[i].y)>>2)-((v[i+2].x-v[i].x)>>2)*((v[i+1].y-v[i].y)>>2);
	}
	sTop=ceil4(v[top].y);
	sBot=ceil4(v[bot].y);
	if(sTop>=sBot || sBot<=0 || sTop>=rh || 0==area)
	{
		return;
	}
	if(sTop<0)
	{
		sTop=0;
	}
	if(sBot>rh)
	{
		sBot=rh;
	}
	/* Chain A walks +1, chain B walks -1.  area>0 means +1 is the right side. */
	eL.end=top; eL.sEnd=sTop-1; eL.cur=top;
	eR.end=top; eR.sEnd=sTop-1; eR.cur=top;
	if(area>0)
	{
		right=&eL;
		left=&eR;
	}
	else
	{
		right=&eR;
		left=&eL;
	}
	/* eL walks +1, eR walks -1.  Rows between vertex events form a
	   trapezoid with fixed edges. */
	{
		int stride=rpitch*rscale;
		void (*trap)(Trap *)=trapFunc[(rscale-1)|((flags&(RP_TRANSPARENT|RP_WRAP))<<1)];
		s=sTop;
		while(s<sBot)
		{
			int end;
			if(!pedge_next(&eL,v,n,1,s,tex) || !pedge_next(&eR,v,n,-1,s,tex))
			{
				break;
			}
			end=MIN(MIN(eL.sEnd,eR.sEnd),sBot);
			if(end<=s)
			{
				break;
			}
			if(tex)
			{
				Trap t;
				t.row=rbuf+s*stride;
				t.stride=stride;
				t.rows=end-s;
				t.lx=left->x16;  t.ldx=left->dx;
				t.lu=left->u;    t.ldu=left->du;
				t.lv=left->v;    t.ldv=left->dv;
				t.rx=right->x16; t.rdx=right->dx;
				t.ru=right->u;   t.rdu=right->du;
				t.rv=right->v;   t.rdv=right->dv;
				t.tile=tile;
				t.maxX=rw;
				t.pixels=0;
				trap(&t);
				g_statPixels+=t.pixels;
				left->x16=t.lx;  left->u=t.lu;  left->v=t.lv;
				right->x16=t.rx; right->u=t.ru; right->v=t.rv;
				s=end;
			}
			else
			{
				u8 *row=rbuf+s*stride;
				if(g_fastFlat)
				{
					for(; s<end; ++s,row+=stride)
					{
						int xl=(left->x16+0x7FFF)>>16,xr=(right->x16+0x7FFF)>>16;
						if(xr>rw) xr=rw;
						if(xl<0) xl=0;
						if(xr>xl) flat_span(row+xl*rscale,(xr-xl)*rscale,flatWord);
						left->x16+=left->dx;
						right->x16+=right->dx;
					}
				}
				else
				for(; s<end; ++s,row+=stride)
				{
					int xl=(left->x16+0x7FFF)>>16,xr=(right->x16+0x7FFF)>>16;
					if(xr>rw) xr=rw;
					if(xl<0) xl=0;
					if(xr>xl)
					{
						BENCH_HOOK(g_benchCnt[S_FLATPX]+=xr-xl);
						memset(row+xl*rscale,flat,(xr-xl)*rscale);
					}
					left->x16+=left->dx;
					right->x16+=right->dx;
				}
			}
		}
	}
}

void raster_poly(const RVert *v,int n,const u8 *tile,int flags)
{
	poly_scan(v,n,tile,flags,0);
}

void raster_flat_poly(const RVert *v,int n,u8 color)
{
	poly_scan(v,n,NULL,0,color);
}

void raster_occluder(const RVert *v,int n,int farDepth,u32 key)
{
	int i,top=0,bot=0,area=0,s,sBot,cell=1<<occShift;
	int band=-1,bandRows=0,bandLeft=0,bandRight=0;
	PEdge eL,eR,*left,*right;
	if(farDepth<=0 || farDepth>=65535) return;
	for(i=1; i<n; ++i)
	{
		if(v[i].y<v[top].y) top=i;
		if(v[i].y>v[bot].y) bot=i;
	}
	for(i=0; i+2<n && !area; ++i)
		area=((v[i+1].x-v[i].x)>>2)*((v[i+2].y-v[i].y)>>2)-((v[i+2].x-v[i].x)>>2)*((v[i+1].y-v[i].y)>>2);
	s=MAX(0,ceil4(v[top].y)); sBot=MIN(rh,ceil4(v[bot].y));
	if(s>=sBot || !area) return;
	eL.end=eR.end=top; eL.sEnd=eR.sEnd=s-1; eL.cur=eR.cur=top;
	left=area>0 ? &eR : &eL; right=area>0 ? &eL : &eR;
	while(s<sBot)
	{
		int end;
		if(!pedge_next(&eL,v,n,1,s,0) || !pedge_next(&eR,v,n,-1,s,0)) break;
		end=MIN(MIN(eL.sEnd,eR.sEnd),sBot);
		if(end<=s) break;
		while(s<end)
		{
			int b=s>>occShift,rows=MIN(end,(b+1)*cell)-s;
			/* Edges are linear between vertex events. Their endpoint extrema
			   give the intersection of the exact spans across this whole band. */
			int lx=MAX(left->x16,left->x16+left->dx*(rows-1));
			int rx=MIN(right->x16,right->x16+right->dx*(rows-1));
			int xl=MAX(0,(lx+0x7FFF)>>16),xr=MIN(rw,(rx+0x7FFF)>>16);
			if(b!=band) { band=b; bandRows=0; bandLeft=xl; bandRight=xr; }
			bandLeft=MAX(bandLeft,xl); bandRight=MIN(bandRight,xr);
			bandRows+=rows;
			if(bandRows==MIN(cell,rh-band*cell))
			{
				int x,x0=(bandLeft+cell-1)>>occShift,x1=bandRight>>occShift;
				for(x=x0; x<x1; ++x)
				{
					int k=band*OCC_COLS+x;
					if(farDepth<occDepth[k] || (farDepth==occDepth[k] && key<occKey[k]))
					{
						if(occDepth[k]==65535) ++g_statOcclusionTiles;
						occDepth[k]=farDepth; occKey[k]=key;
					}
				}
			}
			left->x16+=left->dx*rows; right->x16+=right->dx*rows; s+=rows;
		}
	}
}

void raster_pixel(int x,int y,u8 color)
{
	if((unsigned)x<(unsigned)rw && (unsigned)y<(unsigned)rh)
	{
		if(1==rscale)
		{
			rbuf[y*rpitch+x]=color;
		}
		else
		{
			*(u16 *)(rbuf+y*2*rpitch+x*2)=color|(color<<8);
		}
	}
}

/* Line in 28.4 coordinates, clipped per pixel. */
void raster_line(int x0,int y0,int x1,int y1,u8 color)
{
	int dx,dy,steps,i,x,y,sx,sy;
	x0>>=4; y0>>=4; x1>>=4; y1>>=4;
	dx=x1-x0;
	dy=y1-y0;
	steps=MAX(ABS(dx),ABS(dy));
	if(steps>2000)
	{
		return;
	}
	if(0==steps)
	{
		raster_pixel(x0,y0,color);
		return;
	}
	x=x0<<16;
	y=y0<<16;
	sx=(dx<<16)/steps;
	sy=(dy<<16)/steps;
	for(i=0; i<=steps; ++i)
	{
		raster_pixel((x+0x8000)>>16,(y+0x8000)>>16,color);
		x+=sx;
		y+=sy;
	}
}

void raster_fill_rows(int y0,int y1,u8 color)
{
	int y;
	for(y=MAX(0,y0); y<MIN(rh,y1); ++y)
	{
		if(1==rscale)
		{
			memset(rbuf+y*rpitch,color,rw);
		}
		else
		{
			memset(rbuf+y*2*rpitch,color,rw*2);
		}
	}
}

void raster_finish(void)
{
	int y;
	if(2==rscale)
	{
		for(y=0; y<rh; ++y)
		{
			memcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);
		}
	}
}
