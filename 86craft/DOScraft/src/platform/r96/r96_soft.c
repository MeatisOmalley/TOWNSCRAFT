/* Pentium software rasterizer for the 1996 build (replaces raster.c/trap.S).

   Coverage is the 486 rasterizer's: 28.4 vertices, pixel centers, edges
   stepped in 16.16 from an exact IDIV setup, so the same pixels are drawn.
   What changes is texturing. Polygons from the 3D front end carry w, the
   projection's focal16<<16/z (RP_PERSP); this file fits the polygon's plane
   to q=w/65536, s=u*q and t=v*q (all linear on screen) and r96_span.S maps
   textures with exact perspective at every 16th pixel, overlapping each
   FDIV with integer texel stores. Polygons without w (the held item) keep
   the old edge-interpolated affine mapping.

   The FPU runs at single precision only around the span loops: FDIV is 19
   instead of 39 cycles on a Pentium, and nothing there needs more bits.
   Plane setup and everything else keep the default precision. */
#include "raster.h"
#include "fmath.h"
#include "gfx.h"
#include "bench.h"
#include "far_fill.h"
#include "r96.h"

/* Layout shared with r96_span.S */
typedef struct
{
	u8 *row;
	int stride,rows;
	int lx,ldx,rx,rdx;
	int maxX;
	const u8 *tile;
	u32 pixels;
	float q,s,t;            /* Plane at pixel (0,row) */
	float dqdx,dsdx,dtdx;
	float dqdy,dsdy,dtdy;
} R96Trap;
typedef char r96_trap_layout[(sizeof(R96Trap)==76) ? 1 : -1];

void r96_persp_opaque(R96Trap *t);
void r96_persp_transp(R96Trap *t);
void r96_persp_opaquew(R96Trap *t);
void r96_persp_transpw(R96Trap *t);
/* Indexed by flags&(RP_TRANSPARENT|RP_WRAP) */
static void (*const perspFunc[4])(R96Trap *)=
{
	r96_persp_opaque,r96_persp_transp,r96_persp_opaquew,r96_persp_transpw,
};

static u8 *rbuf;
static int rw,rh,rpitch,rscale,rstride;
u32 g_statPixels;
int g_fastFlat;
u32 g_statOcclusionTiles;
/* Diagnostics and tests: draw perspective spans with the C reference. */
int r96_reference_spans;

static u16 fpuDefault,fpuSingle;

void raster_set_target(u8 *buf,int w,int h,int pitch,int scale)
{
	if(!fpuDefault)
	{
		__asm__ volatile("fnstcw %0":"=m"(fpuDefault));
		fpuSingle=fpuDefault&~0x0300;    /* PC=00: 24-bit mantissa */
	}
	rbuf=buf;
	rw=w;
	rh=h;
	rpitch=pitch;
	rscale=scale;
	rstride=pitch*scale;
}

/* The span loops read the atlas in place. */
void r96_textures_changed(void){}

int raster_width(void){return rw;}
int raster_height(void){return rh;}

/* Occlusion stays off (README); the proof map is not built here. */
void raster_occlusion_reset(void){g_statOcclusionTiles=0;}
void raster_occluder(const RVert *v,int n,int farDepth,u32 key){(void)v;(void)n;(void)farDepth;(void)key;}
int raster_occluded(int minx,int maxx,int miny,int maxy,int nearDepth,u32 key)
{
	(void)minx;(void)maxx;(void)miny;(void)maxy;(void)nearDepth;(void)key;
	return 0;
}

static inline int ceil4(int v)
{
	/* First pixel/scanline whose center (n*16+8) is >= v */
	return (v+7)>>4;
}

typedef struct
{
	int x16,dx,u,du,v,dv;
	int cur,end;
	int sEnd;
} PEdge;

static inline void pedge_init(PEdge *e,const RVert *a,const RVert *b,int s,int tex)
{
	int dy=b->y-a->y,pre=s*16+8-a->y;
	e->dx=divshift(b->x-a->x,dy,16);
	e->x16=(a->x<<12)+mulshift(e->dx,pre,4);
	if(tex)
	{
		int du=b->u-a->u,dv=b->v-a->v;
		e->du=du ? divshift(du,dy,4) : 0;
		e->u=a->u+(du ? mulshift(e->du,pre,4) : 0);
		e->dv=dv ? divshift(dv,dy,4) : 0;
		e->v=a->v+(dv ? mulshift(e->dv,pre,4) : 0);
	}
}

static inline int pedge_next(PEdge *e,const RVert *v,int n,int dir,int s,int tex)
{
	int guard=n;
	while(s>=e->sEnd)
	{
		int a,b;
		if(--guard<0) return 0;
		a=e->end; b=a+dir;
		if(b<0) b+=n;
		if(b>=n) b-=n;
		if(v[b].y<v[a].y) return 0;
		e->cur=a;
		e->end=b;
		e->sEnd=ceil4(v[b].y);
		if(s<e->sEnd) pedge_init(e,&v[a],&v[b],s,tex);
	}
	return 1;
}

static inline void flat_span(u8 *dst,int n,u32 color)
{
	int prefix=MIN((-(u32)dst)&3,(u32)n),tail,words;
	if(n<=8)
	{
		while(n-->0) *dst++=(u8)color;
		return;
	}
	n-=prefix;
	while(prefix--) *dst++=(u8)color;
	tail=n&3; words=n>>2;
	__asm__ volatile("rep stosl" : "+D"(dst),"+c"(words) : "a"(color) : "memory");
	while(tail--) *dst++=(u8)color;
}

/* Rows of a convex polygon: top/bottom, orientation and chains. Returns
   0 when nothing is drawn. */
typedef struct
{
	int sTop,sBot;
	PEdge eL,eR,*left,*right;
} Scan;

static int scan_begin(Scan *sc,const RVert *v,int n)
{
	int i,top=0,bot=0,area=0;
	for(i=1; i<n; ++i)
	{
		if(v[i].y<v[top].y) top=i;
		if(v[i].y>v[bot].y) bot=i;
	}
	for(i=0; i+2<n && 0==area; ++i)
		area=((v[i+1].x-v[i].x)>>2)*((v[i+2].y-v[i].y)>>2)-((v[i+2].x-v[i].x)>>2)*((v[i+1].y-v[i].y)>>2);
	sc->sTop=ceil4(v[top].y);
	sc->sBot=ceil4(v[bot].y);
	if(sc->sTop>=sc->sBot || sc->sBot<=0 || sc->sTop>=rh || 0==area) return 0;
	if(sc->sTop<0) sc->sTop=0;
	if(sc->sBot>rh) sc->sBot=rh;
	sc->eL.end=top; sc->eL.sEnd=sc->sTop-1; sc->eL.cur=top;
	sc->eR.end=top; sc->eR.sEnd=sc->sTop-1; sc->eR.cur=top;
	/* Chain eL walks +1, eR walks -1; area>0 means +1 is the right side. */
	sc->right=area>0 ? &sc->eL : &sc->eR;
	sc->left=area>0 ? &sc->eR : &sc->eL;
	return 1;
}

/* Rows [s,end) between two vertex events, or 0 when the chains end. */
static inline int scan_band(Scan *sc,const RVert *v,int n,int s,int tex)
{
	int end;
	if(!pedge_next(&sc->eL,v,n,1,s,tex) || !pedge_next(&sc->eR,v,n,-1,s,tex)) return 0;
	end=MIN(MIN(sc->eL.sEnd,sc->eR.sEnd),sc->sBot);
	return end>s ? end : 0;
}

/* ---------- Flat polygons ---------- */

void raster_flat_poly(const RVert *v,int n,u8 color)
{
	Scan sc;
	int s,end;
	u32 word=(u32)color*0x01010101u;
	if(!scan_begin(&sc,v,n)) return;
	for(s=sc.sTop; s<sc.sBot && (end=scan_band(&sc,v,n,s,0)); )
	{
		u8 *row=rbuf+s*rstride;
		for(; s<end; ++s,row+=rstride)
		{
			int xl=(sc.left->x16+0x7FFF)>>16,xr=(sc.right->x16+0x7FFF)>>16;
			if(xr>rw) xr=rw;
			if(xl<0) xl=0;
			if(xr>xl)
			{
				BENCH_HOOK(if(!g_fastFlat) g_benchCnt[S_FLATPX]+=xr-xl);
				flat_span(row+xl*rscale,(xr-xl)*rscale,word);
			}
			sc.left->x16+=sc.left->dx;
			sc.right->x16+=sc.right->dx;
		}
	}
}

/* ---------- Texels ---------- */

static inline u8 texel(const u8 *tile,int u,int v,int wrap)
{
	int iu=u>>16,iv=v>>16;
	if(wrap) { iu&=15; iv&=15; }
	else { iu=CLAMP(iu,0,15); iv=CLAMP(iv,0,15); }
	return tile[iv*256+iu];
}

static inline void put(u8 *dst,u8 c,int transparent)
{
	if(transparent && !c) return;
	if(2==rscale) *(u16 *)dst=c|(c<<8);
	else *dst=c;
}

/* ---------- Affine polygons (no w: the held item) ---------- */

static void affine_poly(const RVert *v,int n,const u8 *tile,int flags)
{
	Scan sc;
	int s,end,wrap=flags&RP_WRAP,transparent=flags&RP_TRANSPARENT;
	if(!scan_begin(&sc,v,n)) return;
	for(s=sc.sTop; s<sc.sBot && (end=scan_band(&sc,v,n,s,1)); )
	{
		u8 *row=rbuf+s*rstride;
		for(; s<end; ++s,row+=rstride)
		{
			PEdge *l=sc.left,*r=sc.right;
			int xl=(l->x16+0x7FFF)>>16,xr=(r->x16+0x7FFF)>>16,clipped=xl<0,width;
			if(xr>rw) xr=rw;
			if(xl<0) xl=0;
			width=CLAMP((r->x16-l->x16+0x8000)>>16,1,SCR_MAX_W);
			if(xr>xl)
			{
				/* As trap.S: the span gradient, and a prestep from the edge
				   to the first pixel center only where the left edge clips. */
				int du=(r->u-l->u)/width,dv=(r->v-l->v)/width;
				int pre=clipped ? ((xl<<16)+0x8000-l->x16)>>8 : 0;
				int u=l->u+(du>>8)*pre,vv=l->v+(dv>>8)*pre,x;
				g_statPixels+=xr-xl;
				for(x=xl; x<xr; ++x,u+=du,vv+=dv)
					put(row+x*rscale,texel(tile,u,vv,wrap),transparent);
			}
			l->x16+=l->dx; l->u+=l->du; l->v+=l->dv;
			r->x16+=r->dx; r->u+=r->du; r->v+=r->dv;
		}
	}
}

/* ---------- Perspective polygons ---------- */

/* The plane of q,s,t over the polygon, in pixels from pixel (0,0)'s center.
   Fitted to the fan triangle of largest area: every vertex lies on it. */
typedef struct
{
	float q,s,t,dqdx,dsdx,dtdx,dqdy,dsdy,dtdy;
} Plane;

#define Q_SCALE (1.0f/65536.0f)
/* No-wrap u,v range (16.16), as r96_span.S: inside the tile with a margin
   for step rounding, so a segment never steps into the next tile. */
#define UV_LOW 0x80
#define UV_TOP 0xFFF80

static int fit_plane(const RVert *v,int n,Plane *p)
{
	int i,best=1,bestArea=0;
	for(i=1; i+1<n; ++i)
	{
		int area=((v[i].x-v[0].x)>>2)*((v[i+1].y-v[0].y)>>2)-((v[i+1].x-v[0].x)>>2)*((v[i].y-v[0].y)>>2);
		if(ABS(area)>ABS(bestArea)) { bestArea=area; best=i; }
	}
	if(!bestArea) return 0;
	{
		const RVert *a=&v[0],*b=&v[best],*c=&v[best+1];
		float x1=(b->x-a->x)*(1.0f/16),y1=(b->y-a->y)*(1.0f/16);
		float x2=(c->x-a->x)*(1.0f/16),y2=(c->y-a->y)*(1.0f/16);
		float inv=1.0f/(x1*y2-x2*y1);
		float qa=a->w*Q_SCALE,qb=b->w*Q_SCALE,qc=c->w*Q_SCALE;
		float sa=a->u*qa,sb=b->u*qb,sc=c->u*qc;
		float ta=a->v*qa,tb=b->v*qb,tc=c->v*qc;
		/* Pixel (0,0)'s center relative to vertex a */
		float ox=0.5f-a->x*(1.0f/16),oy=0.5f-a->y*(1.0f/16);
		p->dqdx=((qb-qa)*y2-(qc-qa)*y1)*inv; p->dqdy=((qc-qa)*x1-(qb-qa)*x2)*inv;
		p->dsdx=((sb-sa)*y2-(sc-sa)*y1)*inv; p->dsdy=((sc-sa)*x1-(sb-sa)*x2)*inv;
		p->dtdx=((tb-ta)*y2-(tc-ta)*y1)*inv; p->dtdy=((tc-ta)*x1-(tb-ta)*x2)*inv;
		p->q=qa+p->dqdx*ox+p->dqdy*oy;
		p->s=sa+p->dsdx*ox+p->dsdy*oy;
		p->t=ta+p->dtdx*ox+p->dtdy*oy;
	}
	return 1;
}

static inline int round_float(float f)
{
	int i;
	__asm__("fistpl %0":"=m"(i):"t"(f):"st");
	return i;
}

/* The span loop of r96_span.S in C, on the same segment endpoints: exact
   u,v at every 16th pixel and at the last one, linear in between. Serves
   the doubled presets and the native tests. */
static void persp_span_c(u8 *dst,int x,int count,const R96Trap *t,int flags)
{
	int wrap=flags&RP_WRAP,transparent=flags&RP_TRANSPARENT;
	float q=t->q+t->dqdx*x,s=t->s+t->dsdx*x,tt=t->t+t->dtdx*x;
	int u0=round_float(s/q),v0=round_float(tt/q);
	if(!wrap) { u0=CLAMP(u0,UV_LOW,UV_TOP); v0=CLAMP(v0,UV_LOW,UV_TOP); }
	while(count>0)
	{
		int len=count>16 ? 16 : count,k=count>16 ? 16 : count-1,i,u1,v1,du=0,dv=0;
		if(k>0)
		{
			float q1=q+t->dqdx*k,s1=s+t->dsdx*k,t1=tt+t->dtdx*k;
			u1=round_float(s1/q1); v1=round_float(t1/q1);
			if(!wrap) { u1=CLAMP(u1,UV_LOW,UV_TOP); v1=CLAMP(v1,UV_LOW,UV_TOP); }
			du=round_float((float)(u1-u0)/k); dv=round_float((float)(v1-v0)/k);
			q=q1; s=s1; tt=t1;
		}
		else { u1=u0; v1=v0; }
		for(i=0; i<len; ++i,dst+=rscale)
			put(dst,texel(t->tile,u0+i*du,v0+i*dv,wrap),transparent);
		u0=u1; v0=v1;
		count-=len;
	}
}

static void persp_poly(const RVert *v,int n,const u8 *tile,int flags)
{
	Scan sc;
	Plane p;
	R96Trap t;
	int s,end,useAsm;
	if(!fit_plane(v,n,&p) || !scan_begin(&sc,v,n)) return;
	useAsm=1==rscale && !r96_reference_spans;
	t.stride=rstride;
	t.maxX=rw;
	t.tile=tile;
	t.pixels=0;
	t.q=p.q+p.dqdy*sc.sTop; t.s=p.s+p.dsdy*sc.sTop; t.t=p.t+p.dtdy*sc.sTop;
	t.dqdx=p.dqdx; t.dsdx=p.dsdx; t.dtdx=p.dtdx;
	t.dqdy=p.dqdy; t.dsdy=p.dsdy; t.dtdy=p.dtdy;
	t.row=rbuf+sc.sTop*rstride;
	if(useAsm) __asm__ volatile("fldcw %0"::"m"(fpuSingle));
	for(s=sc.sTop; s<sc.sBot && (end=scan_band(&sc,v,n,s,0)); )
	{
		t.rows=end-s;
		t.lx=sc.left->x16; t.ldx=sc.left->dx;
		t.rx=sc.right->x16; t.rdx=sc.right->dx;
		if(useAsm)
		{
			perspFunc[flags&(RP_TRANSPARENT|RP_WRAP)](&t);
		}
		else
		{
			for(; t.rows>0; --t.rows)
			{
				int xl=(t.lx+0x7FFF)>>16,xr=(t.rx+0x7FFF)>>16;
				if(xr>rw) xr=rw;
				if(xl<0) xl=0;
				if(xr>xl)
				{
					t.pixels+=xr-xl;
					persp_span_c(t.row+xl*rscale,xl,xr-xl,&t,flags);
				}
				t.lx+=t.ldx; t.rx+=t.rdx; t.row+=t.stride;
				t.q+=t.dqdy; t.s+=t.dsdy; t.t+=t.dtdy;
			}
		}
		sc.left->x16=t.lx; sc.right->x16=t.rx;
		s=end;
	}
	if(useAsm) __asm__ volatile("fldcw %0"::"m"(fpuDefault));
	g_statPixels+=t.pixels;
}

void raster_poly(const RVert *v,int n,const u8 *tile,int flags)
{
	if(flags&RP_PERSP) persp_poly(v,n,tile,flags);
	else affine_poly(v,n,tile,flags);
}

/* ---------- Points, lines, rows ---------- */

void raster_pixel(int x,int y,u8 color)
{
	if((unsigned)x<(unsigned)rw && (unsigned)y<(unsigned)rh)
	{
		if(1==rscale) rbuf[y*rpitch+x]=color;
		else *(u16 *)(rbuf+y*2*rpitch+x*2)=color|(color<<8);
	}
}

void raster_line(int x0,int y0,int x1,int y1,u8 color)
{
	int dx,dy,steps,i,x,y,sx,sy;
	x0>>=4; y0>>=4; x1>>=4; y1>>=4;
	dx=x1-x0;
	dy=y1-y0;
	steps=MAX(ABS(dx),ABS(dy));
	if(steps>2000) return;
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
	u32 word=(u32)color*0x01010101u;
	for(y=MAX(0,y0); y<MIN(rh,y1); ++y)
		flat_span(rbuf+y*rstride,rw*rscale,word);
}

void raster_finish(void)
{
	int y;
	if(2==rscale)
		for(y=0; y<rh; ++y)
			memcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);
}

/* ---------- Distant terrain columns (far_render.inc) ---------- */

void raster_far_column(FarColumn *c,int x,int width,int top)
{
	c->mode=-1; c->top=top; c->x=x; c->scale=rscale;
	if((unsigned)x>=(unsigned)rw || x+width>rw) return;
	c->stride=rstride; c->mode=width*rscale;
	c->cursor=rbuf+top*c->stride+x*rscale;
}

void raster_far_pattern(int x,int width,int y0,int y1,u8 a,u8 b,int phase)
{
	FarColumn c;
	y0=MAX(0,y0); y1=MIN(rh,y1);
	if(y1<=y0) return;
	raster_far_column(&c,x,width,y1);
	far_fill_column(&c,y0,a,b,phase);
}

void raster_far_strip(int x,int width,int y0,int y1,u8 color)
{
	u8 *p;
	int y;
	if((unsigned)x>=(unsigned)rw) return;
	y0=MAX(0,y0); y1=MIN(rh,y1);
	p=rbuf+y0*rstride+x*rscale;
	for(y=y0; y<y1; ++y,p+=rstride) flat_span(p,width*rscale,(u32)color*0x01010101u);
}
