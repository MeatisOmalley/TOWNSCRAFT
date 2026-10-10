/* Read-only-original-renderer diagnostic, NOT a production fix or timing test.
 * Identical texture neighborhoods at different 64 KiB address phases should
 * give identical images. Padding deliberately identifies stray atlas reads. */
#include "raster.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t seed=0x8713529u;
static uint32_t next(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
int main(void)
{
    unsigned char a[512*240+32],b[sizeof(a)];
    unsigned char *memory0=malloc(4*65536),*memory1=malloc(4*65536);
    unsigned char *tile0,*tile1;
    int i,j,scale;
    if(!memory0 || !memory1) return 1;
    tile0=(unsigned char *)(((uintptr_t)memory0+65535)&~(uintptr_t)65535)+65536+0x2000;
    tile1=(unsigned char *)(((uintptr_t)memory1+65535)&~(uintptr_t)65535)+65536+0xf000;
    for(i=-65536;i<65536;++i) {
        uint32_t relative=(uint32_t)(i+65536);
        unsigned char color=(unsigned char)((relative>>12)*47+(relative&255)*43+17);
        if(relative%7==0) color=0;
        tile0[i]=tile1[i]=color;
    }
    for(scale=1;scale<=2;++scale) for(i=0;i<12000;++i) {
        int w=320/scale,h=200/scale,flags=i%4;
        int x=(int)(next()%(w+120))-60,y=(int)(next()%(h+100))-50;
        int width=1+next()%w,height=1+next()%h;
        int skew=(i%3)?(int)(next()%65)-32:0;
        int uv=(flags&RP_WRAP)?16*(1+next()%15)*65536-1:16*65536-1;
        unsigned int pixels;
        RVert v[4]={{x*16,y*16,0,0},{(x+width)*16,y*16,uv,0},
            {(x+width+skew)*16,(y+height)*16,uv,uv},{(x+skew)*16,(y+height)*16,0,uv}};
        for(j=0;j<4;++j) { v[j].x+=(i%3)?next()%16:0; v[j].y+=next()%8; }
        if(i&1) { RVert t=v[1]; v[1]=v[3]; v[3]=t; }
        memset(a,197,sizeof(a)); memcpy(b,a,sizeof(a));
        raster_set_target(a+16,w,h,512,scale); g_statPixels=0;
        raster_poly(v,4,tile0+(i%16)*16,flags); pixels=g_statPixels;
        raster_set_target(b+16,w,h,512,scale); g_statPixels=0;
        raster_poly(v,4,tile1+(i%16)*16,flags);
        if(memcmp(a,b,sizeof(a)) || pixels!=g_statPixels) {
            fprintf(stderr,"LAYOUT_SENSITIVE case=%d scale=%d flags=%d tile0=%p tile1=%p\n",
                i,scale,flags,(void *)tile0,(void *)tile1);
            for(j=0;j<4;++j) fprintf(stderr,"vertex %d: x=%d y=%d u=%d v=%d\n",j,v[j].x,v[j].y,v[j].u,v[j].v);
            return 2;
        }
    }
    free(memory0); free(memory1);
    puts("PASS: 24000 original-raster images independent of texture address phase"); return 0;
}
