/* Native x86 checks of the Voodoo backend (r96_raster3dfx.c + r96_sst1.c)
 * against a model of the SST-1 that receives every register and texture
 * write. The model follows 86Box's Voodoo Graphics emulation: float
 * register conversion, vertex A/B/C span walk and rounding, start values at
 * vertex A with gradients, perspective S/W,T/W sampling at a fixed LOD,
 * clamp/wrap, P8 palette, chroma key, color1, clipping and fast fill.
 *
 * Random perspective quads, projected as render.c projects them, must map
 * the exact texel at nearly every pixel (the board divides per pixel),
 * cover what the software rasterizer covers up to edge rounding, keep
 * transparent texels out, and use few PCI writes. */
#include "raster.h"
#include "textures.h"
#include "gfx.h"
#include "far_fill.h"
void raster_far_strip(int x,int width,int y0,int y1,u8 color);
#include "r96.h"
#include "r96_sst1.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32 r96_palette[256];
int r96_palette_dirty=1;
u32 r96_palette_version=1;
u8 *g_texAtlas;
volatile u32 *r96_sst_unused;

#include "r96_sst_model.inc"

/* ---------------- Scenes ---------------- */

#define FOCAL16 (172*16)
#define CX16 (160*8)
#define CY16 (100*8)
static u8 page[FB_PITCH*SCR_H];
static u32 seed=777;
static u32 rng(void){seed=seed*1103515245u+12345u;return seed>>8;}
static int pick(int lo,int hi){return lo+(int)(rng()%(u32)(hi-lo+1));}

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

static int fail(const char *what,int scene){printf("FAIL: %s (scene %d)\n",what,scene);return 1;}

/* Palette: entry i is a distinct color, so a pixel names its texel. */
static void set_palette(void)
{
	/* Distinct in RGB565: red carries the low 5 bits, green the high 3. */
	for(int i=0;i<256;++i) r96_palette[i]=((u32)(i&31)<<19)|((u32)(i>>5)<<10)|0x40u;
	r96_palette_dirty=1; ++r96_palette_version;
}

static int texel_at(int x,int y)
{
	u16 c=fb[y][x];
	for(int i=0;i<256;++i) if(rgb565(r96_palette[i])==c) return i;
	return -1;
}

int main(int argc,char **argv)
{
	if(argc>2) { traceX=atoi(argv[1]); traceY=atoi(argv[2]); }
	static u8 atlasStore[(NUM_SHADED_TEXTURES+1)*4096];
	long covered=0,inexact=0,maxErr=0,coverDiff=0,polys=0,polyWrites=0;
	g_texAtlas=(u8 *)(((uintptr_t)atlasStore+4095)&~(uintptr_t)4095);
	/* Every tile's texels encode their coordinates; level 3 of texture 1
	   has holes at (0,0). */
	for(int t=0;t<NUM_SHADED_TEXTURES;++t) for(int l=0;l<16;++l)
		for(int v=0;v<16;++v) for(int u=0;u<16;++u)
			TEX_TILE(t,l)[v*256+u]=(u8)(v*16+u ? v*16+u : (t==1 && l==3 ? 0 : 1));
	set_palette();
	raster_set_target(page,320,200,FB_PITCH,1);
	r96_textures_changed();
	for(int sc=0;sc<1500;++sc)
	{
		int wrapCells=pick(0,3)==0 ? pick(2,16) : 1,wrap=wrapCells>1;
		int transparent=pick(0,4)==0;
		const u8 *tile=transparent ? TEX_TILE(1,3) : TEX_TILE(pick(2,NUM_SHADED_TEXTURES-1),pick(0,15));
		RVert v[4];
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
		/* Background: palette entry 255 is never a texel color here */
		for(int y=0;y<480;++y) for(int x=0;x<640;++x) fb[y][x]=0xFFFF;
		long before=writes;
		raster_poly(v,4,tile,flags);
		polyWrites+=writes-before; ++polys;
		/* Exact plane in logical pixels from the renderer's own triangle */
		int best=1,bestA=0;
		for(int i=1;i+1<4;++i){
			int a=((v[i].x-v[0].x)>>2)*((v[i+1].y-v[0].y)>>2)-((v[i+1].x-v[0].x)>>2)*((v[i].y-v[0].y)>>2);
			if(abs(a)>abs(bestA)){bestA=a;best=i;}
		}
		const RVert *A=&v[0],*B=&v[best],*C=&v[best+1];
		double x1=(B->x-A->x)/16.0,y1=(B->y-A->y)/16.0,x2=(C->x-A->x)/16.0,y2=(C->y-A->y)/16.0;
		double inv=1.0/(x1*y2-x2*y1);
		double qa=A->w,qb=B->w,qc=C->w;
		double sa=A->u/65536.0*qa,sb=B->u/65536.0*qb,scc=C->u/65536.0*qc;
		double ta=A->v/65536.0*qa,tb=B->v/65536.0*qb,tc=C->v/65536.0*qc;
		double dqx=((qb-qa)*y2-(qc-qa)*y1)*inv,dqy=((qc-qa)*x1-(qb-qa)*x2)*inv;
		double dsx=((sb-sa)*y2-(scc-sa)*y1)*inv,dsy=((scc-sa)*x1-(sb-sa)*x2)*inv;
		double dtx=((tb-ta)*y2-(tc-ta)*y1)*inv,dty=((tc-ta)*x1-(tb-ta)*x2)*inv;
		/* Software coverage of the same polygon, as a reference */
		static u8 flat[FB_PITCH*VIEW_H];
		memset(flat,0,sizeof(flat));
		{
			/* Draw the flat reference through a plain 8-bit model of the
			   backend's coverage: polygon edges at logical pixel centers. */
			for(int y=0;y<VIEW_H;++y) for(int x=0;x<SCR_W;++x)
			{
				double px=x+0.5,py=y+0.5;
				int inside=1;
				for(int i=0;i<4 && inside;++i)
				{
					const RVert *a=&v[i],*b=&v[(i+1)%4];
					double cross=(b->x-a->x)/16.0*(py-a->y/16.0)-(b->y-a->y)/16.0*(px-a->x/16.0);
					if((bestA>0 && cross<0) || (bestA<0 && cross>0)) inside=0;
				}
				flat[y*FB_PITCH+x]=(u8)inside;
			}
		}
		for(int y=0;y<480;++y) for(int x=0;x<640;++x)
		{
			/* Voodoo pixel center in logical pixels */
			double lx=(x+0.5)/2,ly=(y+0.5)/2;
			int got=fb[y][x]==0xFFFF ? -1 : texel_at(x,y);
			int lxi=(int)lx,lyi=(int)ly;
			if(y>=400) { if(got>=0) return fail("3D drawn into the HUD rows",sc); continue; }
			int inRef=flat[lyi*FB_PITCH+lxi];
			double X=lx-A->x/16.0,Y=ly-A->y/16.0,q=qa+dqx*X+dqy*Y;
			double u=(sa+dsx*X+dsy*Y)/q,vv=(ta+dtx*X+dty*Y)/q;
			int eu=ifloor(u),ev=ifloor(vv);
			if(!wrap){eu=eu<0?0:eu>15?15:eu;ev=ev<0?0:ev>15?15:ev;}
			eu&=15; ev&=15;
			if(got<0)
			{
				if(inRef && !(transparent && 0==eu+ev)) ++coverDiff;
				continue;
			}
			if(!inRef) { ++coverDiff; continue; }
			if(transparent && (got&255)==0) return fail("transparent texel drawn",sc);
			++covered;
			int expected=ev*16+eu,err=99;
			/* Opaque tiles store texel (0,0) as 1, like (1,0): either may be meant. */
			for(int alt=0;alt<2;++alt)
			{
				int gu=got&15,gv=got>>4;
				if(alt) { if(1!=got || transparent) break; gu=0; gv=0; }
				int du=abs(gu-eu),dv=abs(gv-ev);
				if(wrap){du=du>8?16-du:du;dv=dv>8?16-dv:dv;}
				if((du>dv?du:dv)<err) err=du>dv?du:dv;
			}
			if(err) ++inexact;
			if(err>maxErr) maxErr=err;
			/* Per-pixel division: off by more than one texel only where the
			   texture is minified (a texel smaller than a Voodoo pixel). */
			double Xr=X+0.5,Yd=Y+0.5,qr=qa+dqx*Xr+dqy*Y,qd=qa+dqx*X+dqy*Yd;
			double ur=(sa+dsx*Xr+dsy*Y)/qr,vr=(ta+dtx*Xr+dty*Y)/qr;
			double ud=(sa+dsx*X+dsy*Yd)/qd,vd=(ta+dtx*X+dty*Yd)/qd;
			int sharp=fabs(ur-u)<1 && fabs(vr-vv)<1 && fabs(ud-u)<1 && fabs(vd-vv)<1;
			if(err>1 && sharp)
			{
				printf("pixel %d,%d got %d expected %d (u=%.3f v=%.3f)\n",x,y,got,expected,u,vv);
				{
					double ax=vtx[0]/16.0,ay=vtx[1]/16.0,ddx=x+0.5-ax,ddy=y+0.5-ay;
					double ms=(stw[0]+stw[3]*ddx+stw[6]*ddy)/4294967296.0;
					double mw=(stw[2]+stw[5]*ddx+stw[8]*ddy)/4294967296.0;
					printf("  model A=(%.3f,%.3f) u=S/W/16=%.3f\n",ax,ay,ms/mw/16);
					for(int i=0;i<4;++i) printf("  v%d x=%.3f y=%.3f w=%d u=%.3f v=%.3f\n",i,v[i].x/8.0,v[i].y/8.0,v[i].w,v[i].u/65536.0,v[i].v/65536.0);
				}
				return fail("texel far from exact perspective",sc);
			}
		}
	}
	printf("covered=%ld inexact=%ld (%.3f%%) maxErr=%ld coverage-diff=%ld (%.3f%%) triangles=%ld\n",
		covered,inexact,100.0*inexact/covered,maxErr,coverDiff,100.0*coverDiff/covered,triangles);
	printf("PCI writes per textured quad: %.1f\n",(double)polyWrites/polys);
	if(inexact*100>covered*3) return fail("over 3% of texels differ from exact perspective",-1);
	if(coverDiff*100>covered*3) return fail("coverage differs from the polygon by over 3%",-1);

	/* Fast fills: one logical pixel is a 2x2 block; rows span the view. */
	for(int y=0;y<480;++y) for(int x=0;x<640;++x) fb[y][x]=0xFFFF;
	raster_pixel(10,20,7);
	raster_fill_rows(100,102,9);
	if(texel_at(20,40)!=7 || texel_at(21,41)!=7 || fb[39][20]!=0xFFFF || fb[40][22]!=0xFFFF)
		return fail("raster_pixel is not one 2x2 block",-1);
	if(texel_at(0,200)!=9 || texel_at(639,203)!=9 || fb[199][0]!=0xFFFF || fb[204][0]!=0xFFFF)
		return fail("raster_fill_rows does not cover exactly its rows",-1);
	/* Flat polygon after fills (clip restored to the view) */
	{
		RVert q[4]={{16*10,16*10,0,0,0},{16*30,16*10,0,0,0},{16*30,16*20,0,0,0},{16*10,16*20,0,0,0}};
		raster_flat_poly(q,4,33);
		if(texel_at(30,30)!=33 || texel_at(59,39)!=33 || fb[40][60]!=0xFFFF)
			return fail("flat polygon",-1);
	}
	/* Distant terrain layer: a strip, flushed by the next polygon */
	{
		RVert q[3]={{16*200,16*150,0,0,0},{16*210,16*150,0,0,0},{16*200,16*160,0,0,0}};
		raster_far_strip(50,4,60,70,44);
		raster_flat_poly(q,3,45);
		if(texel_at(100,120)!=44 || texel_at(107,139)!=44 || fb[118][100]!=0xFFFF || fb[120][108]!=0xFFFF)
			return fail("distant terrain strip",-1);
		if(fb[120][98]!=0xFFFF || fb[140][100]!=0xFFFF) return fail("distant terrain leaked",-1);
	}
	/* Affine polygon (held item): no w, still exact at its vertices */
	{
		for(int y=0;y<480;++y) for(int x=0;x<640;++x) fb[y][x]=0xFFFF;
		RVert q[4]={{16*100,16*50,0x200,0x200,0},{16*164,16*50,(16<<16)-0x200,0x200,0},
			{16*164,16*114,(16<<16)-0x200,(16<<16)-0x200,0},{16*100,16*114,0x200,(16<<16)-0x200,0}};
		raster_poly(q,4,TEX_TILE(2,0),0);
		if(texel_at(201,101)!=1 || texel_at(326,101)!=15 || texel_at(201,226)!=15*16 || texel_at(326,226)!=255)
			return fail("affine polygon corners",-1);
	}
	printf("PASS: 1500 Voodoo scenes, fills, far layer, affine\n");
	return 0;
}
