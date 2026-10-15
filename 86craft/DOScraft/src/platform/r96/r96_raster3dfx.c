/* Voodoo Graphics backend of the raster API (1996 build, D963DFX.EXE).

   The 3D front end is the release's: painter's order, culling and
   projection are unchanged, so the Voodoo needs no depth buffer and draws
   the same faces in the same order as the software renderers. What the
   board takes over is every pixel: perspective-correct textures at 640x400
   from the same 28.4 vertices (one logical pixel is a 2x2 Voodoo block at
   full resolution) and the same palettized, pre-lit 16x16 tiles, uploaded
   once as P8 textures. Texel 0 stays transparent through the chroma key.

   CPU work per polygon is the plane setup of q=1/z, s=u*q and t=v*q (the
   SST-1 needs host gradients), done once per polygon: every triangle of a
   fan from the topmost vertex shares vertex A, its start values and the
   gradients, so only the first triangle sends them. Later triangles write
   two vertices and the command. Registers go out in ascending order
   (A,B,C 0x88-0x9c, starts 0xb4-0xbc, gradients 0xd4-0xfc, command 0x100)
   so the chipset can merge PCI writes into bursts, and repeated state is
   never resent. The plane's one divide overlaps the first triangle's six
   vertex writes, which are integer moves of the float bits: an FPU store
   would wait for the divide.

   2D effects that the game draws into its 8-bit page are composited by
   r96_gfx3dfx.c; distant terrain is drawn into an 8-bit layer here and sent
   as 64x64 texture tiles, each drawn only over its content. */
#include "raster.h"
#include "gfx.h"
#include "textures.h"
#include "bench.h"
#include "far_fill.h"
#include "r96.h"
#include "r96_sst1.h"

u32 g_statPixels;
int g_fastFlat;
u32 g_statOcclusionTiles;
int r96_scale_x=2,r96_scale_y=2;
int r96_frame_3d;

#define VIEW_PIXELS_H 400       /* Voodoo rows of the 3D view */

/* TMU memory map */
#define TEX_ATLAS 0x20000u      /* (texture*16+level)*256 */
#define TEX_ICON  0xC0000u      /* two 256-byte slots */
#define TEX_FAR   0x100000u     /* two sets of FAR_TILES 64x64 tiles */

static int vw,vh;
static float toX,toY;           /* Voodoo pixels per 28.4 unit */
static const float one=1.0f;

/* Register state as last written (~0: unknown) */
static u32 curPath=~0u,curMode=~0u,curTexMode=~0u,curBase=~0u,curLod=~0u,curColor=~0u;
static int clipView;            /* clip registers hold the 3D view */
static u32 chromaKeyRGB;
static int atlasDirty=1;

static inline void set_path(u32 v) { if(v!=curPath) { sst_w(SST_FBZCOLORPATH,v); curPath=v; } }
static inline void set_mode(u32 v) { if(v!=curMode) { sst_w(SST_FBZMODE,v); curMode=v; } }
static inline void set_texmode(u32 v) { if(v!=curTexMode) { sst_w(SST_TEXTUREMODE,v); curTexMode=v; } }
static inline void set_lod(u32 v) { if(v!=curLod) { sst_w(SST_TLOD,v); curLod=v; } }
static inline void set_base(u32 v) { if(v!=curBase) { sst_w(SST_TEXBASE,v); curBase=v; } }
static inline void set_color(u32 v) { if(v!=curColor) { sst_w(SST_COLOR1,v); curColor=v; } }

#define MODE_BASE (SST_ZM_CLIP|SST_ZM_RGB_WRITE|SST_ZM_DRAW_BACK)
#define LOD_16 (16u|(16u<<6))   /* LOD 4: 16x16 */
#define LOD_64 (8u|(8u<<6))     /* LOD 2: 64x64 */

static void clip_view(void)
{
	if(clipView) return;
	sst_w(SST_CLIP_LR,R96_SCREEN_W);
	sst_w(SST_CLIP_Y,VIEW_PIXELS_H);
	clipView=1;
}

/* ---------- Textures ---------- */

/* Texture palette: the display palette, with index 0 (transparent texels)
   on a color no other entry has, which the chroma key then drops. */
static void upload_palette(void)
{
	u32 pal[256];
	int i,unique=0;
	for(i=0; i<256; ++i) pal[i]=r96_palette[i];
	for(chromaKeyRGB=0xFF00FF; !unique; chromaKeyRGB-=0x010001)
	{
		unique=1;
		for(i=1; i<256; ++i)
			if(r96_rgb565(pal[i])==r96_rgb565(chromaKeyRGB)) { unique=0; break; }
		if(unique) break;
	}
	pal[0]=chromaKeyRGB;
	r96_sst1_palette(pal);
	sst_w(SST_CHROMAKEY,chromaKeyRGB);
	r96_palette_dirty=0;
}

void r96_textures_changed(void)
{
	atlasDirty=1;
}

static void upload_atlas(void)
{
	int t,l;
	set_texmode(SST_TM_LOCAL|SST_TM_P8|SST_TM_PERSP);
	set_lod(LOD_16);
	for(t=0; t<NUM_SHADED_TEXTURES; ++t)
		for(l=0; l<16; ++l)
			r96_sst1_texture16(TEX_ATLAS+(t*16+l)*256u,TEX_TILE(t,l),256);
	curBase=~0u;
	atlasDirty=0;
}

/* Tiles outside the atlas (the held item's icon page): hashed and sent to
   alternating slots, so a slot is never rewritten under queued triangles. */
static u32 iconHash=~0u;
static const u8 *iconTile;
static int iconSlot;

static u32 tile_address(const u8 *tile)
{
	u32 off=(u32)(tile-g_texAtlas),hash=2166136261u;
	int y,x;
	if(off<NUM_SHADED_TEXTURES*4096u && !(off&0xF0F))
		return TEX_ATLAS+((off>>12)*16+((off>>4)&15))*256u;
	for(y=0; y<16; ++y) for(x=0; x<16; ++x) hash=(hash^tile[y*256+x])*16777619u;
	if(hash!=iconHash || tile!=iconTile)
	{
		iconSlot^=1;
		set_texmode(SST_TM_LOCAL|SST_TM_P8|SST_TM_PERSP);
		set_lod(LOD_16);
		r96_sst1_texture16(TEX_ICON+iconSlot*256u,tile,256);
		curBase=~0u;
		iconHash=hash; iconTile=tile;
	}
	return TEX_ICON+iconSlot*256u;
}

/* ---------- Distant terrain layer ---------- */

#define FAR_TILE 64
#define FAR_COLS ((SCR_W+FAR_TILE-1)/FAR_TILE)
#define FAR_ROWS ((VIEW_H+FAR_TILE-1)/FAR_TILE)
static u8 farLayer[FAR_ROWS*FAR_TILE][FAR_COLS*FAR_TILE];
static int farTop=VIEW_H,farBottom;
/* What each tile last sent: its slot (of two) and texels. An unchanged tile
   is drawn from that slot again without a download; a changed one goes to
   the other slot, which no queued triangle still reads. */
typedef struct { int slot,valid,r0,r1,c0,c1; u8 texels[FAR_TILE*FAR_TILE]; } FarSent;
static FarSent farSent[FAR_ROWS*FAR_COLS];

static void flush_far(void);
static inline void before_draw(void)
{
	if(farBottom>farTop) flush_far();
}

void raster_far_column(FarColumn *c,int x,int width,int top)
{
	c->mode=-1; c->top=top; c->x=x; c->scale=1;
	if((unsigned)x>=(unsigned)vw || x+width>vw) return;
	c->stride=sizeof(farLayer[0]); c->mode=width;
	c->cursor=&farLayer[top][x];
	/* The march fills upward from top; widen the dirty band to the view. */
	if(farTop>0) farTop=0;
	if(farBottom<top) farBottom=top;
}

void raster_far_pattern(int x,int width,int y0,int y1,u8 a,u8 b,int phase)
{
	FarColumn c;
	y0=MAX(0,y0); y1=MIN(vh,y1);
	if(y1<=y0) return;
	raster_far_column(&c,x,width,y1);
	far_fill_column(&c,y0,a,b,phase);
	farTop=MIN(farTop,y0);
}

void raster_far_strip(int x,int width,int y0,int y1,u8 color)
{
	int y;
	if((unsigned)x>=(unsigned)vw) return;
	y0=MAX(0,y0); y1=MIN(vh,y1);
	width=MIN(width,vw-x);
	for(y=y0; y<y1; ++y) memset(&farLayer[y][x],color,width);
	if(y1>y0) { farTop=MIN(farTop,y0); farBottom=MAX(farBottom,y1); }
}

/* ---------- Triangles ---------- */

/* A vertex on the Voodoo, as floats and their bits */
typedef union { float f; u32 u; } FU;
typedef struct { FU x,y; int ix,iy; } P2;   /* ix,iy: 28.4 for ordering */

/* SST-1 triangle: A has the smallest y, then B, then C; the sign tells on
   which side of AC the vertex B lies. Ordered with the integer 28.4 values,
   so no FPU instruction is involved. */
static inline u32 order_bc(const P2 *a,const P2 **b,const P2 **c)
{
	const P2 *t;
	if((*c)->iy<(*b)->iy) { t=*b; *b=*c; *c=t; }
	return ((*b)->ix-a->ix)*((*c)->iy-a->iy)-((*c)->ix-a->ix)*((*b)->iy-a->iy)<0 ? 0x80000000u : 0;
}

static inline void emit_bc(const P2 *a,const P2 *b,const P2 *c)
{
	u32 sign=order_bc(a,&b,&c);
	sst_w(SST_FVERTEX_BX,b->x.u);
	sst_w(SST_FVERTEX_BY,b->y.u);
	sst_w(SST_FVERTEX_CX,c->x.u);
	sst_w(SST_FVERTEX_CY,c->y.u);
	sst_w(SST_FTRIANGLE,sign);
}

static int to_screen(const RVert *v,int n,P2 *p)
{
	int i,top=0;
	for(i=0; i<n; ++i)
	{
		p[i].x.f=v[i].x*toX;
		p[i].y.f=v[i].y*toY;
		p[i].ix=v[i].x;
		p[i].iy=v[i].y;
		if(v[i].y<v[top].y) top=i;
	}
	return top;
}

/* Untextured fan: vertices only (color1). */
static void flat_fan(const P2 *p,int n,int top)
{
	int k;
	sst_w(SST_FVERTEX_AX,p[top].x.u);
	sst_w(SST_FVERTEX_AY,p[top].y.u);
	for(k=1; k+1<n; ++k)
		emit_bc(&p[top],&p[(top+k)%n],&p[(top+k+1)%n]);
}

void raster_flat_poly(const RVert *v,int n,u8 color)
{
	P2 p[12];
	int top;
	if(n<3) return;
	before_draw();
	clip_view();
	set_path(SST_CP_RGB_COLOR1);
	set_mode(MODE_BASE);
	set_color(r96_palette[color]);
	top=to_screen(v,n,p);
	flat_fan(p,n,top);
}

/* Plane of S/W=u*16*q, T/W=v*16*q, 1/W=q over Voodoo pixels (the TMU
   samples texel (S/W)>>4 of a 16x16 tile). Affine polygons (no w) use q=1
   per triangle instead. */
typedef struct { float dsdx,dtdx,dqdx,dsdy,dtdy,dqdy; } Grad;
#define Q_SCALE (1.0f/16777216.0f)     /* w<=focal16<<16/NEAR_Z keeps q below 1 */
#define ST_SCALE (16.0f/65536.0f)      /* 16.16 texels to 16x16-tile S/W units */

/* 1/det, and the six vertex registers of the first triangle (A, B, C as
   float bits) sent while the FDIV runs. */
static inline float divide_sending_abc(float det,const u32 *abc)
{
#ifdef R96_SST_HOOK
	int i;
	for(i=0; i<6; ++i) sst_w(SST_FVERTEX_AX+i*4,abc[i]);
	return one/det;
#else
	float inv;
	__asm__ volatile(
		"fdivrs %3\n\t"
		"movl (%2),%%eax\n\tmovl %%eax,0x88(%1)\n\t"
		"movl 4(%2),%%eax\n\tmovl %%eax,0x8c(%1)\n\t"
		"movl 8(%2),%%eax\n\tmovl %%eax,0x90(%1)\n\t"
		"movl 12(%2),%%eax\n\tmovl %%eax,0x94(%1)\n\t"
		"movl 16(%2),%%eax\n\tmovl %%eax,0x98(%1)\n\t"
		"movl 20(%2),%%eax\n\tmovl %%eax,0x9c(%1)"
		: "=t"(inv) : "r"(r96_sst),"r"(abc),"m"(one),"0"(det) : "eax","memory");
	return inv;
#endif
}

static inline void gradients(const float *q,const float *s,const float *t,int a,int b,int c,
	float x1,float y1,float x2,float y2,float inv,Grad *g)
{
	g->dqdx=((q[b]-q[a])*y2-(q[c]-q[a])*y1)*inv; g->dqdy=((q[c]-q[a])*x1-(q[b]-q[a])*x2)*inv;
	g->dsdx=((s[b]-s[a])*y2-(s[c]-s[a])*y1)*inv; g->dsdy=((s[c]-s[a])*x1-(s[b]-s[a])*x2)*inv;
	g->dtdx=((t[b]-t[a])*y2-(t[c]-t[a])*y1)*inv; g->dtdy=((t[c]-t[a])*x1-(t[b]-t[a])*x2)*inv;
}

/* Parameters at vertex A, then the gradients (0xb4..0xfc). */
static inline void send_params(float s,float t,float q,const Grad *g)
{
	sst_f(SST_FSTART_S,s);
	sst_f(SST_FSTART_T,t);
	sst_f(SST_FSTART_W,q);
	sst_f(SST_FDSDX,g->dsdx);
	sst_f(SST_FDTDX,g->dtdx);
	sst_f(SST_FDWDX,g->dqdx);
	sst_f(SST_FDSDY,g->dsdy);
	sst_f(SST_FDTDY,g->dtdy);
	sst_f(SST_FDWDY,g->dqdy);
}

/* One textured triangle (A, B, C) with gradients fitted to triangle
   (fa,fb,fc) of the same plane; returns 0 when degenerate. */
static int textured_triangle(const P2 *p,const float *q,const float *s,const float *t,
	int a,int b,int c,int fa,int fb,int fc)
{
	const P2 *A=&p[a],*B=&p[b],*C=&p[c];
	float x1=p[fb].x.f-p[fa].x.f,y1=p[fb].y.f-p[fa].y.f;
	float x2=p[fc].x.f-p[fa].x.f,y2=p[fc].y.f-p[fa].y.f;
	float det=x1*y2-x2*y1,inv,dx,dy;
	u32 abc[6],sign;
	Grad g;
	if(det>-0.01f && det<0.01f) return 0;
	sign=order_bc(A,&B,&C);
	abc[0]=A->x.u; abc[1]=A->y.u; abc[2]=B->x.u; abc[3]=B->y.u; abc[4]=C->x.u; abc[5]=C->y.u;
	inv=divide_sending_abc(det,abc);
	gradients(q,s,t,fa,fb,fc,x1,y1,x2,y2,inv,&g);
	/* Values at vertex A, on the plane */
	dx=A->x.f-p[fa].x.f; dy=A->y.f-p[fa].y.f;
	send_params(s[fa]+g.dsdx*dx+g.dsdy*dy,t[fa]+g.dtdx*dx+g.dtdy*dy,q[fa]+g.dqdx*dx+g.dqdy*dy,&g);
	sst_w(SST_FTRIANGLE,sign);
	return 1;
}

void raster_poly(const RVert *v,int n,const u8 *tile,int flags)
{
	P2 p[12];
	float q[12],s[12],t[12];
	int i,top;
	u32 texMode;
	if(n<3) return;
	before_draw();
	if(atlasDirty) upload_atlas();
	clip_view();
	texMode=SST_TM_LOCAL|SST_TM_P8|SST_TM_PERSP|((flags&RP_WRAP) ? 0 : SST_TM_CLAMP_S|SST_TM_CLAMP_T);
	set_texmode(texMode);
	set_lod(LOD_16);
	set_base(r96_sst1_texbase16(tile_address(tile)));
	set_path(SST_CP_RGB_TEXTURE|SST_CP_TEXTURE|SST_CP_PARAM_ADJUST);
	set_mode(MODE_BASE|((flags&RP_TRANSPARENT) ? SST_ZM_CHROMAKEY : 0));
	top=to_screen(v,n,p);
	for(i=0; i<n; ++i)
	{
		q[i]=(flags&RP_PERSP) ? v[i].w*Q_SCALE : 1.0f;
		s[i]=v[i].u*ST_SCALE*q[i];
		t[i]=v[i].v*ST_SCALE*q[i];
	}
	if(flags&RP_PERSP)
	{
		/* One plane for the polygon, fitted to the fan triangle of largest
		   area (every vertex lies on it); later triangles reuse it. */
		int best=1,k;
		float bestArea=0;
		for(i=1; i+1<n; ++i)
		{
			float area=(p[i].x.f-p[0].x.f)*(p[i+1].y.f-p[0].y.f)-(p[i+1].x.f-p[0].x.f)*(p[i].y.f-p[0].y.f);
			if(area<0) area=-area;
			if(area>bestArea) { bestArea=area; best=i; }
		}
		if(!textured_triangle(p,q,s,t,top,(top+1)%n,(top+2)%n,0,best,best+1)) return;
		for(k=2; k+1<n; ++k)
			emit_bc(&p[top],&p[(top+k)%n],&p[(top+k+1)%n]);
	}
	else
	{
		/* Affine (the held item): each fan triangle maps exactly. */
		int k;
		for(k=1; k+1<n; ++k)
		{
			int b=(top+k)%n,c=(top+k+1)%n;
			textured_triangle(p,q,s,t,top,b,c,top,b,c);
		}
	}
}

/* ---------- Fills, points, lines ---------- */

u32 r96_fastfill_pixels;

static void fill_rect(int x0,int y0,int x1,int y1,u8 color)
{
	if(x1<=x0 || y1<=y0) return;
	sst_w(SST_CLIP_LR,((u32)x0<<16)|(u32)x1);
	sst_w(SST_CLIP_Y,((u32)y0<<16)|(u32)y1);
	clipView=0;
	set_mode(MODE_BASE);
	set_color(r96_palette[color]);
	sst_w(SST_FASTFILL,0);
	r96_fastfill_pixels+=(u32)(x1-x0)*(u32)(y1-y0);
}

void raster_fill_rows(int y0,int y1,u8 color)
{
	y0=MAX(0,y0); y1=MIN(vh,y1);
	if(y1<=y0) return;
	before_draw();
	fill_rect(0,y0*r96_scale_y,R96_SCREEN_W,y1*r96_scale_y,color);
}

void raster_pixel(int x,int y,u8 color)
{
	if((unsigned)x>=(unsigned)vw || (unsigned)y>=(unsigned)vh) return;
	before_draw();
	fill_rect(x*r96_scale_x,y*r96_scale_y,(x+1)*r96_scale_x,(y+1)*r96_scale_y,color);
}

/* One logical pixel wide along the minor axis, as two triangles. The
   28.4 ends are snapped to pixel corners like the software line. */
void raster_line(int x0,int y0,int x1,int y1,u8 color)
{
	RVert q[4];
	int dx,dy,steep;
	x0&=~15; y0&=~15; x1&=~15; y1&=~15;
	dx=x1-x0; dy=y1-y0;
	if(ABS(dx)>2000*16 || ABS(dy)>2000*16) return;
	steep=ABS(dy)>ABS(dx);
	if(steep && dy<0) { int t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
	if(!steep && dx<0) { int t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
	/* Extend the major axis to cover the last pixel too. */
	if(steep) y1+=16; else x1+=16;
	q[0].x=x0; q[0].y=y0; q[1].x=x1; q[1].y=y1;
	q[2].x=x1+(steep ? 16 : 0); q[2].y=y1+(steep ? 0 : 16);
	q[3].x=x0+(steep ? 16 : 0); q[3].y=y0+(steep ? 0 : 16);
	raster_flat_poly(q,4,color);
}

/* ---------- Frame ---------- */

/* The 8-bit pages under 3D frames hold R96_KEY in their view rows. A page
   whose last use was a 3D frame needs only the rows its overlays touched
   reset (r96_gfx3dfx.c reports them); any other page is keyed in full. */
typedef struct { u8 *page; int keyed; u8 dirty[VIEW_H]; } KeyPage;
static KeyPage keyPages[3];
static int keyNext;

static KeyPage *key_page(u8 *page,int create)
{
	int i;
	for(i=0; i<3; ++i) if(keyPages[i].page==page) return &keyPages[i];
	if(!create) return 0;
	{
		KeyPage *k=&keyPages[keyNext];
		keyNext=(keyNext+1)%3;
		k->page=page;
		k->keyed=0;
		return k;
	}
}

void r96_3dfx_page_dirty_row(u8 *page,int y)
{
	KeyPage *k=key_page(page,0);
	if(k && (unsigned)y<VIEW_H) k->dirty[y]=1;
}

void r96_3dfx_page_unkeyed(u8 *page)
{
	KeyPage *k=key_page(page,0);
	if(k) k->keyed=0;
}

void raster_set_target(u8 *buf,int w,int h,int pitch,int scale)
{
	int y;
	KeyPage *k=key_page(buf,1);
	(void)scale;
	vw=w; vh=h;
	r96_scale_x=R96_SCREEN_W/w;
	r96_scale_y=VIEW_PIXELS_H/h;
	toX=r96_scale_x/16.0f;
	toY=r96_scale_y/16.0f;
	/* The 3D view is on the Voodoo; the page keeps the key under overlays. */
	for(y=0; y<VIEW_H; ++y)
		if(!k->keyed || k->dirty[y]) memset(buf+y*pitch,R96_KEY,SCR_W);
	memset(k->dirty,0,sizeof(k->dirty));
	k->keyed=1;
	r96_frame_3d=1;
	if(r96_palette_dirty) upload_palette();
	clipView=0;
	farTop=VIEW_H; farBottom=0;
}

int raster_width(void){return vw;}
int raster_height(void){return vh;}

void raster_finish(void)
{
	before_draw();
}

void raster_occlusion_reset(void){g_statOcclusionTiles=0;}
void raster_occluder(const RVert *v,int n,int farDepth,u32 key){(void)v;(void)n;(void)farDepth;(void)key;}
int raster_occluded(int minx,int maxx,int miny,int maxy,int nearDepth,u32 key)
{
	(void)minx;(void)maxx;(void)miny;(void)maxy;(void)nearDepth;(void)key;
	return 0;
}

/* The far layer's dirty rows as 64x64 P8 tiles, each drawn as a chroma-
   keyed quad over the rectangle its content spans (keyed pixels still cost
   the board fill time) and downloaded only when its texels changed; the
   layer is then cleared. */
static void flush_far(void)
{
	int tx,ty,y0=farTop,y1=farBottom;
	farTop=VIEW_H; farBottom=0;
	set_texmode(SST_TM_LOCAL|SST_TM_P8|SST_TM_PERSP|SST_TM_CLAMP_S|SST_TM_CLAMP_T);
	set_lod(LOD_64);
	set_path(SST_CP_RGB_TEXTURE|SST_CP_TEXTURE|SST_CP_PARAM_ADJUST);
	set_mode(MODE_BASE|SST_ZM_CHROMAKEY);
	clip_view();
	for(ty=y0/FAR_TILE; ty*FAR_TILE<y1; ++ty)
		for(tx=0; tx*FAR_TILE<vw; ++tx)
		{
			FarSent *sent=&farSent[ty*FAR_COLS+tx];
			u32 address;
			int y,r0=FAR_TILE,r1=-1,c0=FAR_TILE/4,c1=-1,same;
			const u8 *src=&farLayer[ty*FAR_TILE][tx*FAR_TILE];
			for(y=0; y<FAR_TILE; ++y)
			{
				const u32 *row=(const u32 *)(src+y*sizeof(farLayer[0]));
				int i;
				for(i=0; i<FAR_TILE/4; ++i)
					if(row[i]) { r0=MIN(r0,y); r1=y; c0=MIN(c0,i); c1=MAX(c1,i); }
			}
			if(r1<0) continue;
			same=sent->valid && sent->r0==r0 && sent->r1==r1 && sent->c0==c0 && sent->c1==c1;
			for(y=r0; same && y<=r1; ++y)
				same=!memcmp(sent->texels+y*FAR_TILE+c0*4,src+y*sizeof(farLayer[0])+c0*4,(c1-c0+1)*4);
			if(!same)
			{
				sent->slot^=1;
				sent->valid=1; sent->r0=r0; sent->r1=r1; sent->c0=c0; sent->c1=c1;
			}
			address=TEX_FAR+((u32)sent->slot*FAR_COLS*FAR_ROWS+(u32)(ty*FAR_COLS+tx))*4096u;
			curBase=((address-(65536u+16384u))>>3)&0x7FFFFu;
			sst_w(SST_TEXBASE,curBase);
			if(!same)
			{
				/* LOD 2 download of the used rows: 64 texels, 4 per write */
				for(y=r0; y<=r1; ++y)
				{
					const u32 *row=(const u32 *)(src+y*sizeof(farLayer[0]));
					int i;
					for(i=c0; i<=c1; ++i) sst_tex_w((2u<<17)|((u32)y<<9)|((u32)i<<3),row[i]);
					memcpy(sent->texels+y*FAR_TILE+c0*4,src+y*sizeof(farLayer[0])+c0*4,(c1-c0+1)*4);
				}
			}
			{
				/* Screen rectangle of the used texels; S/W and T/W are 4 per
				   texel of the 64x64 tile (256 across). */
				float sx=(float)r96_scale_x,sy=(float)r96_scale_y;
				float xa=(tx*FAR_TILE+c0*4)*sx,xb=(tx*FAR_TILE+c1*4+4)*sx;
				float ya=(ty*FAR_TILE+r0)*sy,yb=(ty*FAR_TILE+r1+1)*sy;
				Grad g={4.0f/sx,0,0,0,4.0f/sy,0};
				P2 a,b,c,d;
				a.x.f=xa; a.y.f=ya; b.x.f=xb; b.y.f=ya; c.x.f=xb; c.y.f=yb; d.x.f=xa; d.y.f=yb;
				a.ix=d.ix=0; b.ix=c.ix=1; a.iy=b.iy=0; c.iy=d.iy=1;
				sst_w(SST_FVERTEX_AX,a.x.u);
				sst_w(SST_FVERTEX_AY,a.y.u);
				send_params(c0*16.0f,r0*4.0f,1,&g);
				emit_bc(&a,&b,&c);
				emit_bc(&a,&c,&d);
			}
			for(y=r0; y<=r1; ++y) memset((u8 *)src+y*sizeof(farLayer[0]),0,FAR_TILE);
		}
}

/* Back buffer to the page's key pixels, one sample per page pixel (page
   pixel x,y is Voodoo pixel 2x,2y). Rendered colors are exact palette
   colors, so an exact RGB565 inverse suffices. */
void r96_3dfx_read_view(u8 *page,int pitch)
{
	static u8 inverse[65536];
	static u32 builtVersion=~0u;
	int x,y;
	if(builtVersion!=r96_palette_version)
	{
		int i;
		memset(inverse,0,sizeof(inverse));
		for(i=255; i>=1; --i) inverse[r96_rgb565(r96_palette[i])]=(u8)i;
		builtVersion=r96_palette_version;
	}
	r96_sst1_idle();
	for(y=0; y<VIEW_H; ++y)
	{
		u8 *dst=page+y*pitch;
		volatile const u32 *src=(volatile const u32 *)(r96_lfb+(u32)y*2*R96_LFB_PITCH);
		for(x=0; x<SCR_W; ++x)
			if(R96_KEY==dst[x]) dst[x]=inverse[src[x]&0xFFFF];
	}
}
