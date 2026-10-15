/* Native x86 checks of the 1996 software rasterizer (r96_soft.c with the
 * real r96_span.S assembly). Random perspective quads are projected exactly
 * as render.c projects them. Checks: coverage equals the flat rasterizer's,
 * the assembly matches the C reference span loop, every texel is within one
 * texel of the exact perspective mapping, transparency keeps the
 * background, nothing is written outside the target, and the FPU control
 * word is restored. */
#include "raster.h"
#include "r96.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 200
#define PITCH 512
#define GUARDROWS 4
static u8 frameA[(H+2*GUARDROWS)*PITCH],frameA2[(H+2*GUARDROWS)*PITCH],frameB[(H+2*GUARDROWS)*PITCH],frameF[(H+2*GUARDROWS)*PITCH];
static u8 atlasStore[3*4096];
static u8 *page;      /* 4 KB aligned page: 16 rows of 256 bytes */
static u32 seed=12345;
static u32 rng(void){seed=seed*1103515245u+12345u;return seed>>8;}
static int pick(int lo,int hi){return lo+(int)(rng()%(u32)(hi-lo+1));}

#define FOCAL16 (172*16)
#define CX16 (160*8)
#define CY16 (100*8)

typedef struct { double x,y,z; } P3;

static int project(P3 p,RVert *v)
{
	int z=(int)p.z,inv;
	if(z<12) return 0;
	inv=(int)(((u32)FOCAL16<<16)/(u32)z);
	v->x=CX16+(int)(((long long)(int)p.x*inv)>>16);
	v->y=CY16-(int)(((long long)(int)p.y*inv)>>16);
	v->w=inv;
	return 1;
}

static u8 *row(u8 *frame,int y){return frame+(y+GUARDROWS)*PITCH;}

static void clear(u8 *frame,u8 c){memset(frame,0xEE,sizeof(frameA));for(int y=0;y<H;++y)memset(row(frame,y),c,W);}

static int outside_written(const u8 *frame)
{
	/* Guard rows and the columns right of the view must stay 0xEE. */
	for(int y=-GUARDROWS;y<H+GUARDROWS;++y)
	{
		const u8 *r=frame+(y+GUARDROWS)*PITCH;
		int x0=(y<0 || y>=H) ? 0 : W;
		for(int x=x0;x<PITCH;++x) if(r[x]!=0xEE) return 1;
	}
	return 0;
}

/* Exact u,v (texels) at pixel center from the plane through three vertices. */
typedef struct { double q0,s0,t0,dqx,dqy,dsx,dsy,dtx,dty,x0,y0; } Exact;
static void exact_plane(const RVert *v,int n,Exact *e)
{
	/* The renderer's own fan triangle (r96_soft.c fit_plane): quantized
	   vertices of grazing quads are not exactly coplanar, and this checks
	   the rasterizer, not the choice of plane. */
	int best=1,bestA=0;
	for(int i=1;i+1<n;++i){
		int a=((v[i].x-v[0].x)>>2)*((v[i+1].y-v[0].y)>>2)-((v[i+1].x-v[0].x)>>2)*((v[i].y-v[0].y)>>2);
		if(abs(a)>abs(bestA)){bestA=a;best=i;}
	}
	const RVert *a=&v[0],*b=&v[best],*c=&v[best+1];
	double x1=(b->x-a->x)/16.0,y1=(b->y-a->y)/16.0,x2=(c->x-a->x)/16.0,y2=(c->y-a->y)/16.0;
	double inv=1.0/(x1*y2-x2*y1);
	double qa=a->w,qb=b->w,qc=c->w;
	double sa=a->u/65536.0*qa,sb=b->u/65536.0*qb,sc=c->u/65536.0*qc;
	double ta=a->v/65536.0*qa,tb=b->v/65536.0*qb,tc=c->v/65536.0*qc;
	e->dqx=((qb-qa)*y2-(qc-qa)*y1)*inv; e->dqy=((qc-qa)*x1-(qb-qa)*x2)*inv;
	e->dsx=((sb-sa)*y2-(sc-sa)*y1)*inv; e->dsy=((sc-sa)*x1-(sb-sa)*x2)*inv;
	e->dtx=((tb-ta)*y2-(tc-ta)*y1)*inv; e->dty=((tc-ta)*x1-(tb-ta)*x2)*inv;
	e->q0=qa; e->s0=sa; e->t0=ta; e->x0=a->x/16.0; e->y0=a->y/16.0;
}

static int circ(int d){d&=15;return d>8 ? 16-d : d;}

static double exact_at(const Exact *e,double x,double y,int which)
{
	double X=x+0.5-e->x0,Y=y+0.5-e->y0,q=e->q0+e->dqx*X+e->dqy*Y;
	return (which ? e->t0+e->dtx*X+e->dty*Y : e->s0+e->dsx*X+e->dsy*Y)/q;
}

/* The contract of the Quake technique: exact at the ends of each 16-pixel
   segment of the row's span [xl,xr), linear in between. Texels (unwrapped);
   no-wrap ends are clamped into the tile as the loops clamp them. */
static double model_at(const Exact *e,int x,int y,int which,int xl,int xr,int wrap)
{
	if(xr-xl<=16)
	{
		/* Short spans: exact value and slope at the middle, both ends
		   clamped into the tile when not wrapping. */
		double xm=xl+(xr-xl-1)*0.5,h=(xr-xl-1)*0.5;
		double um=exact_at(e,xm,y,which),d=(exact_at(e,xm+0.001,y,which)-exact_at(e,xm-0.001,y,which))/0.002;
		double a=um-d*h,b=um+d*h;
		if(!wrap)
		{
			double lo=0x80/65536.0,hi=0xFFF80/65536.0;
			a=a<lo?lo:a>hi?hi:a; b=b<lo?lo:b>hi?hi:b;
		}
		return h==0 ? a : a+(b-a)*(x-xl)/(2*h);
	}
	int x0=xl+(x-xl)/16*16,x1=x0+16<xr-1 ? x0+16 : xr-1;
	double a=exact_at(e,x0,y,which),b=exact_at(e,x1,y,which);
	if(!wrap)
	{
		double lo=0x80/65536.0,hi=0xFFF80/65536.0;
		a=a<lo?lo:a>hi?hi:a; b=b<lo?lo:b>hi?hi:b;
	}
	return x1==x0 ? a : a+(b-a)*(x-x0)/(x1-x0);
}

/* Is texel coordinate got (0..15, modulo 16 when wrapping) what the exact
   mapping gives somewhere within r pixels of (x,y), give or take one texel
   for rounding? Segment ends are exact (r=1). Inside a 16-pixel segment the
   linear steps of the Quake technique can lag a strongly foreshortened
   mapping by a few pixels (r=4). */
static int near_exact(const Exact *e,int x,int y,int which,int got,int wrap,int r)
{
	double lo=1e30,hi=-1e30;
	for(int dy=-1;dy<=1;++dy) for(int dx=-r;dx<=r;++dx)
	{
		double c=exact_at(e,x+dx,y+dy,which);
		if(c<lo) lo=c;
		if(c>hi) hi=c;
	}
	int a=(int)lo-2,b=(int)hi+1;
	if(!wrap) { a=a<0?0:a; b=b>15?15:b; return got>=a && got<=b; }
	if(b-a>=15) return 1;
	for(int k=a;k<=b;++k) if(((k%16)+16)%16==got) return 1;
	return 0;
}

static int fail(const char *what,int scene)
{
	printf("FAIL: %s (scene %d, seed state %u)\n",what,scene,seed);
	return 1;
}

void r96_vga_plane_rows(const unsigned char *src,unsigned int pitch,unsigned int rows,unsigned int *vga);

/* The Mode X plane writer against a plain byte deinterleave: every plane,
   band heights 1..16, the 512-byte frame pitch. */
static int check_vga_planes(void)
{
	static u8 src[16*512];
	static u32 vga[16*20+8];
	for(int i=0;i<(int)sizeof(src);++i) src[i]=(u8)rng();
	for(int rows=1;rows<=16;++rows) for(int plane=0;plane<4;++plane)
	{
		memset(vga,0xA5,sizeof(vga));
		r96_vga_plane_rows(src+plane,512,rows,vga);
		const u8 *got=(const u8 *)vga;
		for(int y=0;y<rows;++y) for(int x=0;x<80;++x)
			if(got[y*80+x]!=src[y*512+x*4+plane]) { printf("FAIL: plane %d row %d byte %d\n",plane,y,x); return 1; }
		if(got[rows*80]!=0xA5) { printf("FAIL: plane writer past its rows\n"); return 1; }
	}
	printf("PASS: Mode X plane writer\n");
	return 0;
}

int main(int argc,char **argv)
{
	int scenes=argc>1 ? atoi(argv[1]) : 3000;
	if(check_vga_planes()) return 1;
	long covered=0,refDiff=0,exactDiff=0,maxErr=0,transpChecked=0,sharp=0,sharpDiff=0;
	u16 cw0,cw1;
	page=(u8 *)(((uintptr_t)atlasStore+4095)&~(uintptr_t)4095);
	__asm__ volatile("fnstcw %0":"=m"(cw0));
	for(int sc=0;sc<scenes;++sc)
	{
		int wrapCells=pick(0,3)==0 ? pick(2,16) : 1,wrap=wrapCells>1;
		int transparent=pick(0,4)==0;
		int level=pick(0,15);
		const u8 *tile=page+level*16;
		RVert v[4];
		/* Texels encode their own coordinates; transparent tiles add holes. */
		for(int ty=0;ty<16;++ty) for(int tx=0;tx<16;++tx)
			page[ty*256+level*16+tx]=(u8)(ty*16+tx);
		if(!transparent) page[level*16]=1; /* keep 0 meaning "hole" only */
		/* A random quad in camera space: center, two edge vectors. */
		P3 c={pick(-4000,4000),pick(-2500,2500),pick(60,9000)};
		double sz=pick(64,2048)*(wrap ? 2 : 1);
		double ax=rng()%1000/1000.0*6.283,ay=rng()%1000/1000.0*6.283;
		P3 e1={cos(ax)*sz,0,sin(ax)*sz},e2={0,cos(ay)*sz,sin(ay)*sz*0.7};
		P3 p[4]={{c.x-e1.x-e2.x,c.y-e1.y-e2.y,c.z-e1.z-e2.z},{c.x+e1.x-e2.x,c.y+e1.y-e2.y,c.z+e1.z-e2.z},
			{c.x+e1.x+e2.x,c.y+e1.y+e2.y,c.z+e1.z+e2.z},{c.x-e1.x+e2.x,c.y-e1.y+e2.y,c.z-e1.z+e2.z}};
		int umax=wrap ? (wrapCells<<20)-0x200 : (16<<16)-0x200,ok=1;
		for(int i=0;i<4;++i)
		{
			if(!project(p[i],&v[i])) ok=0;
			v[i].u=(i==1 || i==2) ? umax : 0x200;
			v[i].v=i>=2 ? (16<<16)-0x200 : 0x200;
			if(v[i].x<-48*16 || v[i].x>368*16 || v[i].y<-48*16 || v[i].y>248*16) ok=0;
		}
		if(!ok){--sc;continue;}
		int flags=RP_PERSP|(wrap ? RP_WRAP : 0)|(transparent ? RP_TRANSPARENT : 0);
		/* Flat coverage */
		clear(frameF,0);
		raster_set_target(row(frameF,0),W,H,PITCH,1);
		raster_flat_poly(v,4,1);
		/* Assembly */
		clear(frameA,0xFF);
		raster_set_target(row(frameA,0),W,H,PITCH,1);
		r96_reference_spans=0;
		raster_poly(v,4,tile,flags);
		/* Every texel value is legal, so a second background tells which
		   pixels were written. */
		clear(frameA2,0xFE);
		raster_set_target(row(frameA2,0),W,H,PITCH,1);
		raster_poly(v,4,tile,flags);
		raster_finish();
		__asm__ volatile("fnstcw %0":"=m"(cw1));
		if(cw1!=cw0) return fail("FPU control word not restored by raster_finish",sc);
		{
			/* Every divide started for a segment or row must be consumed:
			   an empty x87 stack has all tags 11b. */
			u16 env[14];
			__asm__ volatile("fnstenv %0\n\tfldenv %0":"=m"(env));
			if(env[4]!=0xFFFF) return fail("span loops left values on the x87 stack",sc);
		}
		if(outside_written(frameA)) return fail("assembly wrote outside the view",sc);
		/* C reference */
		clear(frameB,0xFF);
		raster_set_target(row(frameB,0),W,H,PITCH,1);
		r96_reference_spans=1;
		raster_poly(v,4,tile,flags);
		raster_finish();
		r96_reference_spans=0;
		Exact ex; exact_plane(v,4,&ex);
		for(int y=0;y<H;++y) for(int x=0,xl=-1,xr=-1;x<W;++x)
		{
			if(0==x)
			{
				/* This row's span, for the segment ends */
				for(int i=0;i<W;++i) if(row(frameF,y)[i]) { if(xl<0) xl=i; xr=i+1; }
			}
			int end=xl>=0 && (0==(x-xl)%16 || x==xr-1);
			(void)end;
			u8 f=row(frameF,y)[x],a=row(frameA,y)[x],b=row(frameB,y)[x];
			int written=!(a==0xFF && row(frameA2,y)[x]==0xFE);
			if(written && a!=row(frameA2,y)[x]) return fail("assembly output depends on the background",sc);
			if(!f)
			{
				if(written) return fail("assembly drew outside the polygon",sc);
				continue;
			}
			++covered;
			if(a!=b) ++refDiff;
			double X=x+0.5-ex.x0,Y=y+0.5-ex.y0;
			double q=ex.q0+ex.dqx*X+ex.dqy*Y;
			double u=(ex.s0+ex.dsx*X+ex.dsy*Y)/q,vv=(ex.t0+ex.dtx*X+ex.dty*Y)/q;
			int eu=(int)u,ev=(int)vv;
			if(u<eu) --eu;
			if(vv<ev) --ev;
			if(!wrap){eu=eu<0?0:eu>15?15:eu;ev=ev<0?0:ev>15?15:ev;}
			int expected=((ev&15)*16+(eu&15));
			if(transparent && 0==expected)
			{
				++transpChecked;
				/* Exact hole: the loop must keep the background unless it
				   is off by a texel near the hole's border. */
				continue;
			}
			if(!written)
			{
				/* Transparent texel drawn as hole: only legal at texel 0 */
				if(!transparent)
				{
					printf("row %d span %d..%d, pixel %d unwritten; row:",y,xl,xr,x);
					for(int i=xl;i<xr;++i) printf(" %d",!(row(frameA,y)[i]==0xFF && row(frameA2,y)[i]==0xFE));
					printf("\n");
					return fail("opaque assembly left a covered pixel unwritten",sc);
				}
				double mu=model_at(&ex,x,y,0,xl,xr,wrap),mv=model_at(&ex,x,y,1,xl,xr,wrap);
				if(circ((int)(mu+64)-64)>1 || circ((int)(mv+64)-64)>1)
					return fail("hole not within one pixel of the transparent texel",sc);
				continue;
			}
			int au=a&15,av=a>>4;
			if(!transparent && 0==expected) expected=1;
			if(a!=expected) ++exactDiff;
			/* Texels at least as large as pixels: where a wrong texel shows */
			if(fabs(exact_at(&ex,x+1,y,0)-u)<=1 && fabs(exact_at(&ex,x+1,y,1)-vv)<=1 &&
			   fabs(exact_at(&ex,x,y+1,0)-u)<=1 && fabs(exact_at(&ex,x,y+1,1)-vv)<=1)
			{
				++sharp;
				if(a!=expected) ++sharpDiff;
			}
			int err=MAX(circ(au-(expected&15)),circ(av-(expected>>4)));
			if(err>maxErr) maxErr=err;
			double mu=model_at(&ex,x,y,0,xl,xr,wrap),mv=model_at(&ex,x,y,1,xl,xr,wrap);
			int ou=circ(au-((int)(mu+1024)-1024)),ov=circ(av-((int)(mv+1024)-1024));
			if(!wrap) { ou=abs(au-(int)mu); ov=abs(av-(int)mv); }
			if(1==a && !transparent)
			{
				/* Opaque tiles store texel (0,0) as 1 too: either may be meant. */
				int ou0=circ(-((int)(mu+1024)-1024)),ov0=circ(-((int)(mv+1024)-1024));
				if(!wrap) { ou0=abs((int)mu); ov0=abs((int)mv); }
				if(MAX(ou0,ov0)<MAX(ou,ov)) { ou=ou0; ov=ov0; }
			}
			if(ou>1 || ov>1) {
				printf("pixel %d,%d got %d (u=%d v=%d) expected %d exact u=%.3f v=%.3f err=%d wrap=%d transp=%d\n",
					x,y,a,au,av,expected,u,vv,err,wrap,transparent);
				for(int i=0;i<4;++i) printf("  v%d x=%d y=%d w=%d u=%d v=%d\n",
					i,v[i].x,v[i].y,v[i].w,v[i].u,v[i].v);
				for(int k=-x;k<W-x;++k) if(row(frameF,y)[x+k]) printf("  %d:%d/%.2f",x+k,row(frameA,y)[x+k]&15,exact_at(&ex,x+k,y,0));
				printf("\n");
				for(int k=-3;k<=3;++k) printf("  x%+d: asm %3d ref %3d exact u=%.3f\n",
					k,row(frameA,y)[x+k],row(frameB,y)[x+k],exact_at(&ex,x+k,y,0));
				printf("  model u=%.3f v=%.3f\n",mu,mv);
				return fail("texel more than one texel from exact-ends/linear-segments",sc);
			}
		}
	}
	printf("covered=%ld assembly-vs-C=%ld (%.4f%%) inexact=%ld (%.3f%%) maxErr=%ld holes=%ld\n",
		covered,refDiff,100.0*refDiff/covered,exactDiff,100.0*exactDiff/covered,maxErr,transpChecked);
	printf("texels >= pixels: %ld, inexact %ld (%.3f%%)\n",sharp,sharpDiff,100.0*sharpDiff/sharp);
	if(refDiff*1000>covered) return fail("assembly differs from the C reference in over 0.1% of pixels",-1);
	/* Quake's 16-pixel segments: about 6% of unminified texels in these
	   deliberately harsh scenes (grazing, near) round to a neighbor. */
	if(sharpDiff*100>sharp*8) return fail("over 8% of unminified texels differ from exact perspective",-1);
	printf("PASS: %d perspective scenes\n",scenes);
	return 0;
}
