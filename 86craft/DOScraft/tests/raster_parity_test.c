/* Exact comparison of real pinned and candidate raster bodies + real assembly.
 * Host speed is never used as 486 performance evidence. */
#include "raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
void reference_raster_set_target(u8 *,int,int,int,int);
void reference_raster_poly(const RVert *,int,const u8 *,int);
void reference_raster_flat_poly(const RVert *,int,u8);
void reference_raster_finish(void);
void reference_raster_occlusion_reset(void);
void reference_raster_occluder(const RVert *,int,int,u32);
int reference_raster_occluded(int,int,int,int,int,u32);
extern int reference_g_fastFlat;
extern u32 reference_g_statPixels,reference_g_statOcclusionTiles;
static unsigned int seed=0x8713529u;
static unsigned int next(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
int main(void)
{
    unsigned char a[512*240+32],b[sizeof(a)];
    unsigned char *allocation=malloc(65536);
    unsigned char *tile=(unsigned char *)(((uintptr_t)allocation+16383)&~(uintptr_t)4095);
    int i,j,scale;
    if(!allocation) return 1;
    for(i=0;i<65536;++i) allocation[i]=(i%7==0)?0:(unsigned char)(i*43+17);
    for(scale=1;scale<=2;++scale) for(i=0;i<12000;++i) {
        int w=320/scale,h=200/scale,flags=i%4,flat=(i%5)==0;
        int x=(int)(next()%(w+120))-60,y=(int)(next()%(h+100))-50;
        int width=1+next()%w,height=1+next()%h;
        int skew=(i%3)?(int)(next()%65)-32:0;
        RVert v[4]={{x*16,y*16,0,0},{(x+width)*16,y*16,0,0},
            {(x+width+skew)*16,(y+height)*16,0,0},{(x+skew)*16,(y+height)*16,0,0}};
        /* Subpixel edge slopes, both orientations, flat/textured, clip bounds,
         * repeated UVs and transparent zeros. No-wrap stays inside its tile. */
        int uv=(flags&RP_WRAP)?(16*(1+next()%15)*65536-1):(16*65536-1);
        v[1].u=v[2].u=uv; v[2].v=v[3].v=uv;
        for(j=0;j<4;++j) { v[j].x+=(i%3)?next()%16:0; v[j].y+=next()%8; }
        if(i&1) { RVert t=v[1]; v[1]=v[3]; v[3]=t; }
        memset(a,197,sizeof(a)); memcpy(b,a,sizeof(a));
        reference_raster_set_target(a+16,w,h,512,scale);
        raster_set_target(b+16,w,h,512,scale);
        reference_g_fastFlat=g_fastFlat=i&1;
        reference_g_statPixels=g_statPixels=0;
        if(flat) {
            reference_raster_flat_poly(v,4,(u8)i); raster_flat_poly(v,4,(u8)i);
        } else {
            reference_raster_poly(v,4,tile,flags); raster_poly(v,4,tile,flags);
        }
        reference_raster_finish(); raster_finish();
        if(memcmp(a,b,sizeof(a)) || reference_g_statPixels!=g_statPixels) {
            fprintf(stderr,"pixel mismatch case %d scale %d flags %d\n",i,scale,flags); return 1;
        }
        for(j=0;j<16;++j) if(a[j]!=197) return 1;
        for(j=512*240+16;j<(int)sizeof(a);++j) if(a[j]!=197) return 1;
        reference_raster_occlusion_reset(); raster_occlusion_reset();
        reference_raster_occluder(v,4,200,47); raster_occluder(v,4,200,47);
        if(reference_g_statOcclusionTiles!=g_statOcclusionTiles) return 1;
        for(j=0;j<8;++j) {
            int x0=next()%(w*16),y0=next()%(h*16),dx=next()%128,dy=next()%128;
            int old=reference_raster_occluded(x0,x0+dx,y0,y0+dy,300,48);
            int current=raster_occluded(x0,x0+dx,y0,y0+dy,300,48);
            if(old!=current) { fputs("occlusion mismatch\n",stderr); return 1; }
        }
    }
    free(allocation);
    puts("PASS: 24000 polygons, exact framebuffer/guards/stats and occlusion decisions"); return 0;
}
