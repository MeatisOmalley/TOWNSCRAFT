/* Host i386 parity: production vs candidate raster.c + trap.S + low_planar.S,
 * chunky scale 1/2 and planar Low targets, flat and textured polygons. */
#include "raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
void reference_raster_set_target(u8 *,int,int,int,int);
void reference_raster_poly(const RVert *,int,const u8 *,int);
void reference_raster_flat_poly(const RVert *,int,u8);
void reference_raster_finish(void);
void reference_raster_complete_odd_rows(void);
void reference_raster_set_odd_copy_policy(int,int,int,int,int);
void raster_complete_odd_rows(void);
void raster_set_odd_copy_policy(int,int,int,int,int);
extern int reference_g_fastFlat;
extern u32 reference_g_statPixels;
int dos_very_low_mode;
static u8 planeA[38400*2+64],planeB[sizeof(planeA)];
static u8 *planeNext;
unsigned char *dos_video_planar_target(void) { return planeNext; }
static const unsigned char *lastRows;
void dos_video_planar_frame(const unsigned char *rows) { lastRows=rows; }
static unsigned int seed=0x8713529u;
static unsigned int next(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
int main(int argc,char **argv)
{
    static unsigned char a[512*240+32],b[sizeof(a)];
    unsigned char *allocation=malloc(4<<20);
    unsigned char *page=(unsigned char *)(((uintptr_t)allocation+65535)&~(uintptr_t)65535);
    int cases=argc>1?atoi(argv[1]):12000,i,j,mode,polys=0;
    if(!allocation) return 1;
    for(i=0;i<(3<<20);++i) page[i]=(i%7==0)?0:(unsigned char)(i*43+17);
    for(mode=0;mode<3;++mode) for(i=0;i<cases;++i) {
        int planar=mode==2,scale=planar?2:1+mode,w=320/scale,h=200/scale,flags=i%4,flat=(i%3)==0;
        int pitch=planar?320:512,k,n=1+(i%5==0?next()%6:0);
        const unsigned char *tile=page+(next()%512)*4096+(next()%16)*16;
        memset(a,197,sizeof(a)); memcpy(b,a,sizeof(a));
        memset(planeA,91,sizeof(planeA)); memcpy(planeB,planeA,sizeof(planeA));
        reference_raster_set_odd_copy_policy(planar,i&1,i&2,i&4,i%9==0);
        raster_set_odd_copy_policy(planar,i&1,i&2,i&4,i%9==0);
        planeNext=planeA+16; reference_raster_set_target(a+16,w,h,pitch,scale);
        planeNext=planeB+16; raster_set_target(b+16,w,h,pitch,scale);
        reference_g_fastFlat=g_fastFlat=(i>>2)&1;
        reference_g_statPixels=g_statPixels=0;
        for(k=0;k<n;++k,++polys) {
            int x=(int)(next()%(w+120))-60,y=(int)(next()%(h+100))-50;
            int width=1+next()%(i%4?12:w),height=1+next()%(i%3?12:h);
            int skew=(i%3)?(int)(next()%65)-32:0;
            RVert v[4]={{x*16,y*16,0,0},{(x+width)*16,y*16,0,0},
                {(x+width+skew)*16,(y+height)*16,0,0},{(x+skew)*16,(y+height)*16,0,0}};
            int uv=(flags&RP_WRAP)?(16*(1+next()%15)*65536-1):(16*65536-1);
            v[1].u=v[2].u=uv; v[2].v=v[3].v=uv;
            for(j=0;j<4;++j) { v[j].x+=next()%16; v[j].y+=next()%16; }
            if(next()&1) { RVert t=v[1]; v[1]=v[3]; v[3]=t; }
            if(flat) { reference_raster_flat_poly(v,4,(u8)(i+k)); raster_flat_poly(v,4,(u8)(i+k)); }
            else { reference_raster_poly(v,4,tile,flags); raster_poly(v,4,tile,flags); }
        }
        reference_raster_finish(); raster_finish();
        if(memcmp(a,b,sizeof(a)) || memcmp(planeA,planeB,sizeof(planeA)) || reference_g_statPixels!=g_statPixels) {
            fprintf(stderr,"mismatch mode %d case %d\n",mode,i); return 2;
        }
        reference_raster_complete_odd_rows(); raster_complete_odd_rows();
        if(memcmp(a,b,sizeof(a))) { fprintf(stderr,"readback mismatch mode %d case %d\n",mode,i); return 3; }
    }
    free(allocation);
    printf("PASS: %d polygons in chunky scale 1/2 and planar targets, exact frames/planes/stats\n",polys); return 0;
}
