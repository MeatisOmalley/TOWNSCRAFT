/* Native probe of whole 1996 frames: the real transformed render.c, world,
 * textures and far field of the D96SOFT build on the prebaked 1024 world,
 * drawn by the software backend (or, with R96_PROBE_3DFX, the Voodoo backend
 * into the SST-1 model of r96_sst_model.inc). Reports the backend's work per
 * frame and writes each view as a PNG for inspection. Host timings only
 * rank costs; they say nothing absolute about a Pentium.
 *
 * usage: probe <terrain image> <output folder> [view distance] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <dpmi.h>
#include "render.h"
#include "world.h"
#include "textures.h"
#include "raster.h"
#include "gfx.h"
#include "sys.h"
#include "far_field.h"
#include "graphics_presets.h"
#include "hdd_backend.h"
#include "heap.h"
#include "r96.h"
#ifdef R96_PROBE_3DFX
#include "r96_sst1.h"
#include "r96_sst_model.inc"
u32 r96_palette[256];
int r96_palette_dirty=1;
u32 r96_palette_version=1;
#endif

__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *);

u32 g_ramMB=128;
volatile u32 g_ticks;
int dos_cpu386;
static const char *terrainPath;
static u32 palette[256];

void fatal(const char *message) { fprintf(stderr,"FATAL: %s\n",message); exit(1); }
void video_set_palette(int i,int r,int g,int b)
{
	palette[i&255]=((u32)r<<16)|((u32)g<<8)|(u32)b;
#ifdef R96_PROBE_3DFX
	r96_palette[i&255]=palette[i&255]; r96_palette_dirty=1; ++r96_palette_version;
#endif
}
void dos_viewmodel_draw(int width,int height) { (void)width; (void)height; }
void gfx_wait_flip(void) {}
int world_test_open(const char *path,int flags,...)
{
	if(strcmp(path,DOS_TERRAIN_PATH) || (flags&(O_CREAT|O_TRUNC))) fatal("unexpected file");
	return open(terrainPath,flags|O_BINARY);
}
int __dpmi_int(int vector,__dpmi_regs *regs) { (void)vector; regs->x.flags=0; return 0; }

/* ---- minimal PNG: stored deflate blocks ---- */
static u32 crcTable[256];
static u32 crc(u32 c,const u8 *p,size_t n)
{
	if(!crcTable[1]) for(u32 i=0;i<256;++i){u32 k=i;for(int j=0;j<8;++j)k=(k&1)?0xEDB88320u^(k>>1):k>>1;crcTable[i]=k;}
	for(size_t i=0;i<n;++i) c=crcTable[(c^p[i])&255]^(c>>8);
	return c;
}
static void be32(FILE *f,u32 v){fputc(v>>24,f);fputc(v>>16,f);fputc(v>>8,f);fputc(v,f);}
static void chunk(FILE *f,const char *type,const u8 *data,u32 n)
{
	be32(f,n); fwrite(type,1,4,f); if(n) fwrite(data,1,n,f);
	be32(f,~crc(crc(~0u,(const u8 *)type,4),data,n));
}
/* rgb(x,y): the probe's image source, 0xRRGGBB */
static const u8 *pngFrame;
static int pngPitch;
static u32 png_rgb(int x,int y)
{
#ifdef R96_PROBE_3DFX
	u16 c=fb[y][x];
	(void)pngFrame; (void)pngPitch;
	return ((u32)(c>>11)<<19)|((u32)((c>>5)&63)<<10)|((u32)(c&31)<<3);
#else
	return palette[pngFrame[y*pngPitch+x]];
#endif
}

static void write_png(const char *path,int w,int h)
{
	FILE *f=fopen(path,"wb");
	size_t raw=(size_t)(w*3+1)*h,blocks=(raw+65534)/65535,n=2+raw+blocks*5+4;
	u8 *z=malloc(n),*p=z,*rows=malloc(raw);
	u32 a=1,b=0;
	for(int y=0;y<h;++y)
	{
		u8 *r=rows+(size_t)y*(w*3+1);
		r[0]=0;
		for(int x=0;x<w;++x){u32 c=png_rgb(x,y);r[1+x*3]=c>>16;r[2+x*3]=c>>8;r[3+x*3]=c;}
	}
	*p++=0x78; *p++=0x01;
	for(size_t off=0;off<raw;off+=65535)
	{
		size_t len=raw-off<65535 ? raw-off : 65535;
		*p++=off+len==raw; *p++=len; *p++=len>>8; *p++=~len; *p++=(~len)>>8;
		memcpy(p,rows+off,len); p+=len;
	}
	for(size_t i=0;i<raw;++i){a=(a+rows[i])%65521;b=(b+a)%65521;}
	*p++=b>>8;*p++=b;*p++=a>>8;*p++=a;
	static const u8 sig[8]={137,80,78,71,13,10,26,10};
	u8 hdr[13]={0};
	hdr[0]=w>>24;hdr[1]=w>>16;hdr[2]=w>>8;hdr[3]=w;hdr[4]=h>>24;hdr[5]=h>>16;hdr[6]=h>>8;hdr[7]=h;hdr[8]=8;hdr[9]=2;
	fwrite(sig,1,8,f); chunk(f,"IHDR",hdr,13); chunk(f,"IDAT",z,(u32)(p-z)); chunk(f,"IEND",0,0);
	fclose(f); free(z); free(rows);
}

static u8 frame[FB_PITCH*SCR_H];

int main(int argc,char **argv)
{
	/* Five places around spawn, four directions each, a little above the
	   ground and pitched slightly down: forest, slopes, water, distance. */
	static const int places[][2]={{0,0},{24,8},{-40,30},{60,-20},{-10,-70}};
	enum { NPLACES=sizeof(places)/sizeof(places[0]),REPEAT=4 };
	R96Stats total;
	u32 totalFaces=0,frames=0;
	double totalHost=0;
	long long freq,t0,t1;
	char path[512];
	int view=argc>3 ? atoi(argv[3]) : 16;
	setvbuf(stdout,0,_IONBF,0);
	if(argc<3) fatal("usage: probe terrain output [view]");
	terrainPath=argv[1];
	{
		u8 *arena=calloc(32*1048576u,1);
		if(!arena || dos_heap_init(arena,2*1048576,arena+2*1048576,30*1048576)) fatal("arenas");
	}
	QueryPerformanceFrequency(&freq);
	/* As game.c: art and palette, world, renderer, then the prebaked seed */
	textures_init();
	palette_apply();
	fprintf(stderr,"world_alloc, low %u high %u\n",heap_low_free(),heap_high_free());
	world_alloc();
	fprintf(stderr,"render_init, low %u high %u\n",heap_low_free(),heap_high_free());
	render_init();
	fprintf(stderr,"world_generate, low %u high %u\n",heap_low_free(),heap_high_free());
	world_generate(4242);
	printf("world %d, spawn %d,%d,%d\n",g_W,g_spawnX,g_spawnY,g_spawnZ);
	g_renderScale=1; g_viewDist=view; g_flatDist=0; g_distanceMode=0;
	g_textureSubdivision=SUBDIV_OFF; g_occlusion=0;
	dos_far_enabled=dos_far_max_level();
	fprintf(stderr,"far map %s, level %d\n",dos_far_available() ? "loaded" : "absent",dos_far_enabled);
#ifndef R96_PROBE_3DFX
	/* R96_REF=1: every span through the C reference, for frame checksums */
	r96_reference_spans=getenv("R96_REF")!=0;
#endif
	memset(&total,0,sizeof(total));
	for(int place=0;place<NPLACES;++place) for(int dir=0;dir<4;++dir)
	{
		int px=g_spawnX+places[place][0],pz=g_spawnZ+places[place][1],ground=-1;
		RenderEnv env;
		memset(&env,0,sizeof(env));
		for(int i=0;i<4000;++i) { world_stream(px,pz,view+8,64); world_update_dirty_chunks(0); }
		/* Stand on the nearest open grass or sand (not in a canopy). */
		for(int r=0;r<16 && ground<0;++r)
			for(int dz=-r;dz<=r && ground<0;++dz) for(int dx=-r;dx<=r && ground<0;++dx)
			{
				int x=px+dx,z=pz+dz,y=WH-1;
				while(y>0 && BLK_ID(wget(x,y,z))==B_AIR) --y;
				if(BLK_ID(wget(x,y,z))==B_GRASS || BLK_ID(wget(x,y,z))==B_SAND) { px=x; pz=z; ground=y+1; }
			}
		if(ground<0) ground=world_height(px,pz)+1;
		g_cam.x=px*256+128; g_cam.z=pz*256+128;
		/* Directions 0-2 at eye height; direction 3 a vista from above. */
		g_cam.y=(dir==3 ? MIN(ground+12,WH-2) : ground)*256+415;
		g_cam.yaw=dir*256+place*40; g_cam.pitch=dir==3 ? -60 : -20;   /* signed, as the game keeps it */
		env.sunAngle=300; env.skyColor=P(R_SKY,11); env.fogColor=P(R_SKY,12);
		env.cloudColor=P(R_GRAY,14);
		/* Warm frame (collection), then repeats of the cached list. */
		render_frame(frame,&env);
#ifdef R96_PROBE_3DFX
		long w0=writes,t0w=triangles,f0=fills,x0=texWrites;
#else
		memset(&r96_stats,0,sizeof(r96_stats));
#endif
		g_statFaces=0;
		QueryPerformanceCounter(&t0);
		/* R96_WALK=1: each frame one block further, as walking forces a new
		   collection (and sort) of the draw list whenever the cell changes */
		for(int k=0;k<REPEAT;++k)
		{
			if(getenv("R96_WALK")) g_cam.x+=(k&1) ? -256 : 256;
			render_frame(frame,&env);
		}
		QueryPerformanceCounter(&t1);
#ifdef R96_PROBE_3DFX
		printf("p%d/%d faces=%u writes=%ld triangles=%ld fills=%ld texwrites=%ld\n",place,dir,g_statFaces,
			(writes-w0)/REPEAT,(triangles-t0w)/REPEAT,(fills-f0)/REPEAT,(texWrites-x0)/REPEAT);
		pngFrame=0;
		snprintf(path,sizeof(path),"%s/v%d-%d.png",argv[2],place,dir);
		write_png(path,640,400);
		totalFaces+=g_statFaces; ++frames;
		totalHost+=(double)(t1-t0)*1e6/freq/REPEAT;
		continue;
#endif
		R96Stats s=r96_stats;
		u32 *a=(u32 *)&s,*b=(u32 *)&total;
		for(unsigned i=0;i<sizeof(s)/4;++i) { a[i]/=REPEAT; b[i]+=a[i]; }
		totalFaces+=g_statFaces; ++frames;
		totalHost+=(double)(t1-t0)*1e6/freq/REPEAT;
		u32 sum=2166136261u;
		for(int y=0;y<VIEW_H;++y) for(int x=0;x<SCR_W;++x) sum=(sum^frame[y*FB_PITCH+x])*16777619u;
		printf("p%d/%d sum=%08x faces=%u persp=%u flat=%u pixel=%u rows=%u spans=%u px=%u flatpx=%u seg=%u\n",
			place,dir,sum,g_statFaces,s.persp,s.flat,s.pixel,s.rows,s.spans,s.pixels,s.flatPixels,s.segments);
		snprintf(path,sizeof(path),"%s/p%d-%d.png",argv[2],place,dir);
		pngFrame=frame; pngPitch=FB_PITCH;
		write_png(path,SCR_W,VIEW_H);
	}
#ifdef R96_PROBE_3DFX
	(void)total;
	printf("MEAN faces=%u host=%.0fus\n",totalFaces/frames,totalHost/frames);
	return 0;
#endif
	{
		R96Stats s=total;
		u32 n=frames;
		printf("MEAN faces=%u persp=%u affine=%u flat=%u pixel=%u rows=%u spans=%u px=%u flatpx=%u seg=%u host=%.0fus\n",
			totalFaces/n,s.persp/n,s.affine/n,s.flat/n,s.pixel/n,s.rows/n,s.spans/n,s.pixels/n,s.flatPixels/n,s.segments/n,totalHost/n);
		printf("MEAN affine-eligible polys=%u rows=%u px=%u\n",s.affinePolys/n,s.affineRows/n,s.affinePixels/n);
		printf("MEAN spans 1:%u 2-3:%u 4-7:%u 8-15:%u 16-31:%u 32+:%u | depth <1%%:%u <2%%:%u <5%%:%u <10%%:%u <25%%:%u more:%u\n",
			s.spanLen[0]/n,s.spanLen[1]/n,s.spanLen[2]/n,s.spanLen[3]/n,s.spanLen[4]/n,s.spanLen[5]/n,
			s.depthRatio[0]/n,s.depthRatio[1]/n,s.depthRatio[2]/n,s.depthRatio[3]/n,s.depthRatio[4]/n,s.depthRatio[5]/n);
	}
	return 0;
}
