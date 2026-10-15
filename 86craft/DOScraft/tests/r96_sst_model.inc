/* Model of the 3dfx Voodoo Graphics (SST-1) for native tests: receives every
 * register and texture write of r96_sst1.h's hooks and draws into fb. It
 * follows 86Box's emulation: float register conversion, vertex A/B/C span
 * walk and rounding, start values at vertex A with gradients, perspective
 * S/W,T/W sampling at the fixed LOD, clamp/wrap, P8 palette, chroma key,
 * color1, clipping and fast fill. Include once; needs r96_sst1.h. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* floor() of this 32-bit mingw runtime misrounds under -fno-builtin; the
   checks only need an exact integer floor of moderate values. */
static int ifloor(double d){int i=(int)d;return d<i ? i-1 : i;}

/* ---------------- SST-1 model ---------------- */

static u32 reg[0x400/4];
static int64_t stw[16];         /* startS,T,W, dSdX.. as 32.32 */
static int vtx[6];              /* Ax,Ay,Bx,By,Cx,Cy in 12.4 */
static u8 tmem[4u<<20];
static u32 pal[256];
static u16 fb[480][640];
static long writes,triangles,fills,texWrites;

static int64_t to_fix32(u32 bits)
{
	union { u32 u; float f; } v;
	v.u=bits;
	return (int64_t)((double)v.f*4294967296.0);
}

static int to_12_4(u32 bits)
{
	union { u32 u; float f; } v;
	v.u=bits;
	return (int16_t)((int32_t)(v.f*16.0f)&0xffff);
}

static u16 rgb565(u32 c){return (u16)(((c>>8)&0xF800)|((c>>5)&0x07E0)|((c>>3)&0x1F));}

static int lod_level(void){return (reg[SST_TLOD>>2]&0x3f)>>2;}

static u32 lod_base(int lod)
{
	u32 base=(reg[SST_TEXBASE>>2]&0x7ffff)<<3,offset=0;
	for(int l=0;l<lod;++l) offset+=(256u>>l)*(256u>>l);
	return base+offset;
}

static int traceX=-1,traceY=-1;
static void shade(int x,int y)
{
	if(x==traceX && y==traceY)
		printf("  trace: triangle %ld A=(%d,%d) B=(%d,%d) C=(%d,%d) S0=%lld dSdX=%lld\n",triangles,
			vtx[0],vtx[1],vtx[2],vtx[3],vtx[4],vtx[5],(long long)stw[0],(long long)stw[3]);
	u32 path=reg[SST_FBZCOLORPATH>>2],rgb;
	if(path&SST_CP_TEXTURE)
	{
		double ax=vtx[0]/16.0,ay=vtx[1]/16.0,dx=x+0.5-ax,dy=y+0.5-ay;
		double s=(stw[0]+stw[3]*dx+stw[6]*dy)/4294967296.0;
		double t=(stw[1]+stw[4]*dx+stw[7]*dy)/4294967296.0;
		double w=(stw[2]+stw[5]*dx+stw[8]*dy)/4294967296.0;
		int lod=lod_level(),size=256>>lod;
		u32 mode=reg[SST_TEXTUREMODE>>2];
		if(w<=0) return;
		int ts=ifloor((s/w)/(1<<lod)),tt=ifloor((t/w)/(1<<lod));
		if(mode&SST_TM_CLAMP_S) ts=ts<0?0:ts>=size?size-1:ts; else ts&=size-1;
		if(mode&SST_TM_CLAMP_T) tt=tt<0?0:tt>=size?size-1:tt; else tt&=size-1;
		rgb=pal[tmem[(lod_base(lod)+ts+(tt<<(8-lod)))&(sizeof(tmem)-1)]];
		if(x==traceX && y==traceY)
			printf("  trace: s/w=%.4f t/w=%.4f ts=%d tt=%d texel=%d base=%x\n",s/w,t/w,ts,tt,
				tmem[(lod_base(lod)+ts+(tt<<(8-lod)))&(sizeof(tmem)-1)],lod_base(lod));
	}
	else rgb=reg[SST_COLOR1>>2]&0xFFFFFF;
	if((reg[SST_FBZMODE>>2]&SST_ZM_CHROMAKEY) && rgb==(reg[SST_CHROMAKEY>>2]&0xFFFFFF)) return;
	fb[y][x]=rgb565(rgb);
}

static void triangle(u32 cmd)
{
	int64_t Ax=vtx[0],Ay=vtx[1],Bx=vtx[2],By=vtx[3],Cx=vtx[4],Cy=vtx[5];
	int xdir=(cmd>>31) ? -1 : 1;
	int64_t dxAB=(By-Ay) ? ((Bx<<12)-(Ax<<12))*16/(By-Ay) : 0;
	int64_t dxAC=(Cy-Ay) ? ((Cx<<12)-(Ax<<12))*16/(Cy-Ay) : 0;
	int64_t dxBC=(Cy-By) ? ((Cx<<12)-(Bx<<12))*16/(Cy-By) : 0;
	int ystart=(int)((Ay+7)>>4),yend=(int)((Cy+7)>>4);
	int clip=reg[SST_FBZMODE>>2]&SST_ZM_CLIP;
	int cl=(reg[SST_CLIP_LR>>2]>>16)&0x3ff,cr=reg[SST_CLIP_LR>>2]&0x3ff;
	int cyl=(reg[SST_CLIP_Y>>2]>>16)&0x3ff,cyh=reg[SST_CLIP_Y>>2]&0x3ff;
	if(!(Ay<=By && By<=Cy)) { printf("FAIL: vertices not sorted by y\n"); exit(1); }
	++triangles;
	for(int y=ystart;y<yend;++y)
	{
		int64_t ry=((int64_t)y<<4)+8;
		int64_t x=(Ax<<12)+((dxAC*(ry-Ay))>>4),x2;
		if(clip && y<cyl) continue;
		if(clip && y>=cyh) break;
		x2=ry<By ? (Ax<<12)+((dxAB*(ry-Ay))>>4) : (Bx<<12)+((dxBC*(ry-By))>>4);
		if(xdir>0) x2-=1<<16; else x-=1<<16;
		int a=(int)((x+0x7000)>>16),b=(int)((x2+0x7000)>>16);
		int lo=xdir>0 ? a : b,hi=xdir>0 ? b : a;
		if(clip) { if(lo<cl) lo=cl; if(hi>=cr) hi=cr-1; }
		if(lo<0) lo=0;
		if(hi>639) hi=639;
		if(y<0 || y>=480) continue;
		for(int px=lo;px<=hi;++px) shade(px,y);
	}
}

void r96_sst_hook(int offset,u32 value)
{
	++writes;
	if(offset>=SST_FVERTEX_AX && offset<=SST_FVERTEX_CY) { vtx[(offset-SST_FVERTEX_AX)>>2]=to_12_4(value); return; }
	switch(offset)
	{
	case SST_FSTART_S: stw[0]=to_fix32(value); return;
	case SST_FSTART_T: stw[1]=to_fix32(value); return;
	case SST_FSTART_W: stw[2]=to_fix32(value); return;
	case SST_FDSDX: stw[3]=to_fix32(value); return;
	case SST_FDTDX: stw[4]=to_fix32(value); return;
	case SST_FDWDX: stw[5]=to_fix32(value); return;
	case SST_FDSDY: stw[6]=to_fix32(value); return;
	case SST_FDTDY: stw[7]=to_fix32(value); return;
	case SST_FDWDY: stw[8]=to_fix32(value); return;
	case SST_FTRIANGLE: triangle(value); return;
	case SST_FASTFILL:
		{
			int cl=(reg[SST_CLIP_LR>>2]>>16)&0x3ff,cr=reg[SST_CLIP_LR>>2]&0x3ff;
			int cyl=(reg[SST_CLIP_Y>>2]>>16)&0x3ff,cyh=reg[SST_CLIP_Y>>2]&0x3ff;
			++fills;
			for(int y=cyl;y<cyh && y<480;++y) for(int x=cl;x<cr && x<640;++x)
				fb[y][x]=rgb565(reg[SST_COLOR1>>2]);
		}
		return;
	case SST_NCC0_I0: case SST_NCC0_I0+4:
		if(value&0x80000000u) pal[((value>>23)&0xfe)|((offset-SST_NCC0_I0)>>2)]=value&0xFFFFFF;
		return;
	}
	if(offset>=0 && offset<0x400) reg[offset>>2]=value;
}

void r96_sst_tex_hook(u32 offset,u32 value)
{
	int lod=(offset>>17)&0xf,t=(offset>>9)&0xff,s=(offset>>1)&0xfc;
	u32 address=lod_base(lod)+s+(t<<(8-lod));
	++texWrites;
	memcpy(&tmem[address&(sizeof(tmem)-1)],&value,4);
}

u32 r96_sst_read_hook(int offset){(void)offset;return 0;}
