/* Actual imported transforms, subdivision, clipping, raster and trap assembly.
   If the world-space proof rejects a target, drawing only the wall must match
   the original far-target-then-wall painter image, including all guards. */
#include "render.h"
#include "raster.h"
#include "world.h"
#include "textures.h"
#include "fmath.h"
#include "bench.h"
#include "wall_visibility.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
Camera g_cam;
int g_viewDist=12,g_renderScale,g_W=256,g_flatDist,g_distanceMode,g_occlusion,g_flatLOD=96;
int g_textureSubdivision;
u32 g_statFaces,g_statOccluded,g_statOccluders;
u8 *g_texAtlas,g_shadeLUT[16][256];
typedef struct { int c[3]; } V3;
#define NPLANES 5
#define NEAR_Z 12
#define GUARD 48
#define INV_NEAR 4096
#define INV_FAR (INV_NEAR+4096*8)
static int occCapture,occQuery,occBudget,skyDarkenCur;
static u32 occCurrentKey;
static int vw,vh,focal16,cx16,cy16,sqTab[256];
static u32 reciprocal[INV_NEAR+4096],*invTab=reciprocal;
static u8 texTransparent[NUM_TEXTURES],texAvg[NUM_TEXTURES];
static u8 dirShade[6]={2,2,3,0,1,1};
#include "corners.inc"
#include "projection.inc"
#include "reference.inc"
static unsigned seed=0x934b8371;
static unsigned next(void) { seed=seed*1664525u+1013904223u;return seed; }
static u8 a[512*240+32],b[sizeof(a)];
int main(void)
{
    int i,j,tested=0;
    u8 *allocation=malloc(NUM_TEXTURES*4096+8192);
    assert(allocation); g_texAtlas=(u8 *)(((uintptr_t)allocation+4095)&~(uintptr_t)4095);
    for(i=0;i<NUM_TEXTURES*4096;++i) g_texAtlas[i]=(u8)(1+next()%254);
    for(i=0;i<16;++i) for(j=0;j<256;++j) g_shadeLUT[i][j]=(u8)(j+i);
    for(i=0;i<256;++i) sqTab[i]=i*i;
    for(i=0;i<100000;++i) {
        DosWallCone cone;
        int axis=i%3,side=i&1?1:-1,dir=axis*2+(side<0),cam[3],wl[3],wh[3],lo[3],hi[3];
        int wc[3]={100,20,100},tc[3],plane,k,near=1;
        g_cam=(Camera){100*256+64+(int)(next()%128),20*256+64+(int)(next()%128),
            100*256+64+(int)(next()%128),(int)(next()%1024),(int)(next()%501)-250};
        if(i%3) {
            g_cam.yaw=axis==0?(side>0?256:768):axis==2?(side>0?0:512):next()%1024;
            g_cam.yaw=(g_cam.yaw+(int)(next()%161)-80)&1023;
            g_cam.pitch=axis==1?(side>0?210:-210):(int)(next()%121)-60;
        }
        cam[0]=g_cam.x;cam[1]=g_cam.y;cam[2]=g_cam.z;
        plane=wc[axis]+side*(1+(int)(next()%5));
        for(k=0;k<3;++k) {wl[k]=(wc[k]-3)*256;wh[k]=(wc[k]+4)*256;tc[k]=wc[k]+(int)(next()%3)-1;}
        wl[axis]=wh[axis]=plane*256;
        tc[axis]=plane+side*(2+(int)(next()%4));
        for(k=0;k<3;++k) {lo[k]=tc[k]*256;hi[k]=(tc[k]+1)*256;}
        if(!dos_wall_cone(&cone,axis,plane*256,wl,wh,cam,10)) continue;
        /* Original union-only proof lost a doubled pixel through a row seam
           here. Keep the exact deterministic camera/target regression. */
        if(i==11177) assert(!dos_wall_hidden(&cone,lo,hi,cam,20));
        if(!dos_wall_hidden(&cone,lo,hi,cam,20)) continue;
        g_renderScale=1+((i/6)&1);g_textureSubdivision=i%7==0?SUBDIV_FULL:SUBDIV_ADAPTIVE;
        g_distanceMode=i%5==0;g_flatDist=i%7==0?10:0;
        reference_setup_camera();
        vw=320/g_renderScale;vh=200/g_renderScale;focal16=172*16/g_renderScale;
        cx16=vw*8;cy16=vh*8;build_inv_table();
        for(k=0;k<8;++k) {
            int x=(k&1?wh[0]:wl[0])/256,y=(k&2?wh[1]:wl[1])/256,z=(k&4?wh[2]:wl[2])/256;
            if(reference_TX[x].c[2]+reference_TY[y].c[2]+reference_TZ[z].c[2]<64) {near=0;break;}
        }
        if(!near) continue;
        for(j=0;j<2;++j) {
            u8 *fb=j?b:a;memset(fb,197,sizeof(a));g_statFaces=g_statPixels=0;
            raster_set_target(fb+16,vw,vh,512,g_renderScale);raster_occlusion_reset();
            if(!j) reference_draw_quad(tc[0],tc[1],tc[2],dir,1,1,3,240);
            if(axis==1) reference_draw_quad(wl[0]/256,plane-(side<0),wl[2]/256,dir,7,7,2,240);
            else for(k=wl[1]/256;k<wh[1]/256;++k)
                reference_draw_quad(axis==0?plane-(side<0):wl[0]/256,k,
                    axis==2?plane-(side<0):wl[2]/256,dir,axis==0?1:7,axis==2?1:7,2,240);
            raster_finish();
        }
        if(memcmp(a,b,sizeof(a))) {
            int count=0,first=-1;
            for(k=0;k<(int)sizeof(a);++k) if(a[k]!=b[k]) {if(first<0) first=k; ++count;}
            fprintf(stderr,"raster proof mismatch case=%d axis=%d side=%d scale=%d cam=%d,%d,%d yaw=%d pitch=%d plane=%d target=%d,%d,%d distanceMode=%d subdiv=%d pixels=%d first=%d (%d,%d) colours=%u/%u\n",
                i,axis,side,g_renderScale,g_cam.x,g_cam.y,g_cam.z,g_cam.yaw,g_cam.pitch,plane,
                tc[0],tc[1],tc[2],g_distanceMode,g_textureSubdivision,count,first,
                (first-16)%512,(first-16)/512,a[first],b[first]);return 2;
        }
        ++tested;
    }
    assert(tested>1000);
    printf("PASS: %d real raster shadow-cone cases, all axes/sides, moving cameras, adaptive/full, both scales and guards\n",tested);
    free(allocation);return 0;
}
