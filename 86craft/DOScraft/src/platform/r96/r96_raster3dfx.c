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
   two vertices and the command. Registers are written in ascending order
   for PCI bursts, and repeated state is never resent.

   2D effects that the game draws into its 8-bit page are composited by
   r96_gfx3dfx.c; distant terrain is drawn into an 8-bit layer here and sent
   as 64x64 texture tiles. */
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
static int farTop=VIEW_H,farBottom,farSet;

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

typedef struct { float x,y; } P2;

/* SST-1 triangle: A has the smallest y, then B, then C; the sign tells
   on which side of AC the vertex B lies. */
static inline void emit_bc(const P2 *a,const P2 *b,const P2 *c)
{
	const P2 *t;
	float area;
	if(c->y<b->y) { t=b; b=c; c=t; }
	area=(b->x-a->x)*(c->y-a->y)-(c->x-a->x)*(b->y-a->y);
	sst_f(SST_FVERTEX_BX,b->x);
	sst_f(SST_FVERTEX_BY,b->y);
	sst_f(SST_FVERTEX_CX,c->x);
	sst_f(SST_FVERTEX_CY,c->y);
	sst_w(SST_FTRIANGLE,area<0 ? 0x80000000u : 0);
}

static int to_screen(const RVert *v,int n,P2 *p)
{
	int i,top=0;
	for(i=0; i<n; ++i)
	{
		p[i].x=v[i].x*toX;
		p[i].y=v[i].y*toY;
		if(v[i].y<v[top].y) top=i;
	}
	return top;
}

/* Untextured fan: vertices only (color1). */
static void flat_fan(const P2 *p,int n,int top)
{
	int k;
	sst_f(SST_FVERTEX_AX,p[top].x);
	sst_f(SST_FVERTEX_AY,p[top].y);
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
   samples texel (S/W)>>4 of a 16x16 tile), fitted to the fan triangle of
   largest area. Affine polygons (no w) use q=1 per triangle instead. */
typedef struct { float s,t,q,dsdx,dtdx,dqdx,dsdy,dtdy,dqdy; } Grad;
#define Q_SCALE (1.0f/16777216.0f)     /* w<=focal16<<16/NEAR_Z keeps q below 1 */
#define ST_SCALE (16.0f/65536.0f)      /* 16.16 texels to 16x16-tile S/W units */

static int gradients(const P2 *p,const float *q,const float *s,const float *t,int a,int b,int c,Grad *g)
{
	float x1=p[b].x-p[a].x,y1=p[b].y-p[a].y,x2=p[c].x-p[a].x,y2=p[c].y-p[a].y;
	float det=x1*y2-x2*y1,inv;
	if(det>-0.01f && det<0.01f) return 0;
	inv=1.0f/det;
	g->dqdx=((q[b]-q[a])*y2-(q[c]-q[a])*y1)*inv; g->dqdy=((q[c]-q[a])*x1-(q[b]-q[a])*x2)*inv;
	g->dsdx=((s[b]-s[a])*y2-(s[c]-s[a])*y1)*inv; g->dsdy=((s[c]-s[a])*x1-(s[b]-s[a])*x2)*inv;
	g->dtdx=((t[b]-t[a])*y2-(t[c]-t[a])*y1)*inv; g->dtdy=((t[c]-t[a])*x1-(t[b]-t[a])*x2)*inv;
	return 1;
}

/* Vertex A with the parameters at it, then the gradients: ascending
   registers 0x88..0xfc, so the chipset can merge them into PCI bursts. */
static inline void send_a(const P2 *a,float s,float t,float q,const Grad *g)
{
	sst_f(SST_FVERTEX_AX,a->x);
	sst_f(SST_FVERTEX_AY,a->y);
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
		/* One plane for the polygon: every vertex lies on it. */
		Grad g;
		int best=1,k;
		float bestArea=0;
		for(i=1; i+1<n; ++i)
		{
			float area=(p[i].x-p[0].x)*(p[i+1].y-p[0].y)-(p[i+1].x-p[0].x)*(p[i].y-p[0].y);
			if(area<0) area=-area;
			if(area>bestArea) { bestArea=area; best=i; }
		}
		if(!gradients(p,q,s,t,0,best,best+1,&g)) return;
		{
			/* Values at the topmost vertex, on the plane */
			float dx=p[top].x-p[0].x,dy=p[top].y-p[0].y;
			send_a(&p[top],s[0]+g.dsdx*dx+g.dsdy*dy,t[0]+g.dtdx*dx+g.dtdy*dy,
			       q[0]+g.dqdx*dx+g.dqdy*dy,&g);
		}
		for(k=1; k+1<n; ++k)
			emit_bc(&p[top],&p[(top+k)%n],&p[(top+k+1)%n]);
	}
	else
	{
		/* Affine (the held item): each fan triangle maps exactly. */
		int k;
		for(k=1; k+1<n; ++k)
		{
			int b=(top+k)%n,c=(top+k+1)%n;
			Grad g;
			if(!gradients(p,q,s,t,top,b,c,&g)) continue;
			send_a(&p[top],s[top],t[top],q[top],&g);
			emit_bc(&p[top],&p[b],&p[c]);
		}
	}
}

/* ---------- Fills, points, lines ---------- */

static void fill_rect(int x0,int y0,int x1,int y1,u8 color)
{
	if(x1<=x0 || y1<=y0) return;
	sst_w(SST_CLIP_LR,((u32)x0<<16)|(u32)x1);
	sst_w(SST_CLIP_Y,((u32)y0<<16)|(u32)y1);
	clipView=0;
	set_mode(MODE_BASE);
	set_color(r96_palette[color]);
	sst_w(SST_FASTFILL,0);
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

void raster_set_target(u8 *buf,int w,int h,int pitch,int scale)
{
	int y;
	(void)scale;
	vw=w; vh=h;
	r96_scale_x=R96_SCREEN_W/w;
	r96_scale_y=VIEW_PIXELS_H/h;
	toX=r96_scale_x/16.0f;
	toY=r96_scale_y/16.0f;
	/* The 3D view is on the Voodoo; the page keeps the key under overlays. */
	for(y=0; y<VIEW_H; ++y) memset(buf+y*pitch,R96_KEY,SCR_W);
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

/* The far layer's dirty rows as 64x64 P8 tiles (alternating sets), each
   drawn as a chroma-keyed quad; the layer is then cleared for next time. */
static void flush_far(void)
{
	int tx,ty,y0=farTop,y1=farBottom;
	u32 set=TEX_FAR+(u32)farSet*FAR_COLS*FAR_ROWS*4096u;
	farTop=VIEW_H; farBottom=0;
	farSet^=1;
	set_texmode(SST_TM_LOCAL|SST_TM_P8|SST_TM_PERSP|SST_TM_CLAMP_S|SST_TM_CLAMP_T);
	set_lod(LOD_64);
	set_path(SST_CP_RGB_TEXTURE|SST_CP_TEXTURE|SST_CP_PARAM_ADJUST);
	set_mode(MODE_BASE|SST_ZM_CHROMAKEY);
	clip_view();
	for(ty=y0/FAR_TILE; ty*FAR_TILE<y1; ++ty)
		for(tx=0; tx*FAR_TILE<vw; ++tx)
		{
			u32 address=set+(u32)(ty*FAR_COLS+tx)*4096u;
			int y,used=0;
			const u8 *src=&farLayer[ty*FAR_TILE][tx*FAR_TILE];
			for(y=0; y<FAR_TILE && !used; ++y)
			{
				const u32 *row=(const u32 *)(src+y*sizeof(farLayer[0]));
				int i;
				for(i=0; i<FAR_TILE/4; ++i) if(row[i]) { used=1; break; }
			}
			if(!used) continue;
			/* LOD 2 base and download: rows of 64 texels, 4 per write */
			curBase=((address-(65536u+16384u))>>3)&0x7FFFFu;
			sst_w(SST_TEXBASE,curBase);
			for(y=0; y<FAR_TILE; ++y)
			{
				const u32 *row=(const u32 *)(src+y*sizeof(farLayer[0]));
				int i;
				for(i=0; i<FAR_TILE/4; ++i) sst_tex_w((2u<<17)|((u32)y<<9)|((u32)i<<3),row[i]);
			}
			{
				/* Screen quad of the tile, S/W over 0..256 */
				float x0=tx*FAR_TILE*r96_scale_x,x1=x0+FAR_TILE*r96_scale_x;
				float yA=ty*FAR_TILE*r96_scale_y,yB=yA+FAR_TILE*r96_scale_y;
				Grad g={0,0,1,256.0f/(x1-x0),0,0,0,256.0f/(yB-yA),0};
				P2 a={x0,yA},b={x1,yA},c={x1,yB},d={x0,yB};
				send_a(&a,0,0,1,&g);
				emit_bc(&a,&b,&c);
				emit_bc(&a,&c,&d);
			}
			for(y=0; y<FAR_TILE; ++y) memset((u8 *)src+y*sizeof(farLayer[0]),0,FAR_TILE);
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
