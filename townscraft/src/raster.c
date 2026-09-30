/* Convex polygon rasterizer.

   Screen coordinates are 28.4 fixed point.  Texture coordinates are 16.16
   texels, interpolated along the polygon edges and then linearly across each
   span, so every edge maps exactly.  Textures repeat every 16 texels (merged
   quads cover several blocks).  Spans are drawn by the assembly loops in
   span.S.

   In scale 2 mode each render pixel is written as two bytes into the frame
   buffer, and raster_finish() copies even rows to odd rows. */
#include "raster.h"
#include "fmath.h"

void span_opaque1(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv);
void span_opaque2(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv);
void span_transp1(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv);
void span_transp2(u8 *dst,int count,const u8 *tile,u32 u,u32 v,int du,int dv);

static u8 *rbuf;
static int rw,rh,rpitch,rscale;
static int recip[SCR_MAX_W+1];
u32 g_statPixels;

void raster_set_target(u8 *buf,int w,int h,int pitch,int scale)
{
	int i;
	rbuf=buf;
	rw=w;
	rh=h;
	rpitch=pitch;
	rscale=scale;
	if(0==recip[1])
	{
		for(i=1; i<=SCR_MAX_W; ++i)
		{
			recip[i]=32768/i;
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

static void poly_scan(const RVert *v,int n,const u8 *tile,int transparent,u8 flat)
{
	int i,top=0,bot=0,area=0,s,sTop,sBot;
	int tex=(NULL!=tile);
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
	/* eL walks +1, eR walks -1 */
	{
		int du=0,dv=0,grad=0;
		int stride=rpitch*rscale;
		u8 *row=rbuf+sTop*stride;
		for(s=sTop; s<sBot; ++s,row+=stride)
		{
			int xl,xr,w;
			if(!pedge_next(&eL,v,n,1,s,tex) || !pedge_next(&eR,v,n,-1,s,tex))
			{
				break;
			}
			xl=(left->x16+0x7FFF)>>16;
			xr=(right->x16+0x7FFF)>>16;
			if(xr>rw) xr=rw;
			if(xl<0) xl=0;
			w=xr-xl;
			if(w>0)
			{
				if(tex)
				{
					/* Texture gradients change slowly from row to row:
					   recompute them every fourth row. */
					if(0==grad)
					{
						int span=(right->x16-left->x16+0x8000)>>16,r;
						if(span<1) span=1;
						if(span>SCR_MAX_W) span=SCR_MAX_W;
						r=recip[span];
						du=mulshift(right->u-left->u,r,15);
						dv=mulshift(right->v-left->v,r,15);
						grad=4;
					}
					--grad;
					g_statPixels+=w;
					if(1==rscale)
					{
						if(transparent)
							span_transp1(row+xl,w,tile,left->u,left->v,du,dv);
						else
							span_opaque1(row+xl,w,tile,left->u,left->v,du,dv);
					}
					else
					{
						if(transparent)
							span_transp2(row+xl*2,w,tile,left->u,left->v,du,dv);
						else
							span_opaque2(row+xl*2,w,tile,left->u,left->v,du,dv);
					}
				}
				else
				{
					memset(row+xl*rscale,flat,w*rscale);
				}
			}
			left->x16+=left->dx;
			right->x16+=right->dx;
			if(tex)
			{
				left->u+=left->du;
				left->v+=left->dv;
				right->u+=right->du;
				right->v+=right->dv;
			}
		}
	}
}

void raster_poly(const RVert *v,int n,const u8 *tile,int transparent)
{
	poly_scan(v,n,tile,transparent,0);
}

void raster_flat_poly(const RVert *v,int n,u8 color)
{
	poly_scan(v,n,NULL,0,color);
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
