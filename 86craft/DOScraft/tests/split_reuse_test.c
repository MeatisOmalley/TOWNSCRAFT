#include "render.h"
#include "raster.h"
#include "world.h"
#include "textures.h"
#include "fmath.h"
#include "bench.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
Camera g_cam;
int g_viewDist,g_renderScale,g_W,g_flatDist,g_distanceMode,g_occlusion,g_flatLOD=96;
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
static unsigned project_calls;
typedef struct { RVert v[12]; int n,flags,flat,nearDepth,farDepth; intptr_t tile; } Packet;
static Packet packets[2][1024];
static unsigned packet_count[2],side;
static void project(int,int,int,int *,int *);
static void draw_spoly(RVert *,int,const u8 *,int,u8,int,int);
#include "projection.inc"
static void project(int x,int y,int z,int *sx,int *sy)
{ ++project_calls; actual_project(x,y,z,sx,sy); }
static void draw_spoly(RVert *v,int n,const u8 *tile,int flags,u8 flat,int nd,int fd)
{
    assert(n<=12 && packet_count[side]<1024);
    Packet *p=&packets[side][packet_count[side]++];
    memset(p,0,sizeof(*p)); memcpy(p->v,v,n*sizeof(*v));
    p->n=n; p->flags=flags;p->flat=flat;p->nearDepth=nd;p->farDepth=fd;
    p->tile=tile ? tile-g_texAtlas : -1;
    actual_spoly(v,n,tile,flags,flat,nd,fd);
}
#include "reference.inc"
#include "candidate.inc"
static u32 seed=0x92cc13f;
static u32 next(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static u8 a[512*240+32],b[sizeof(a)];
int main(void)
{
    unsigned i,j,totalOld=0,totalNew=0,savedCases=0,qualityChanges=0;
    u8 *allocation=malloc(NUM_TEXTURES*4096+16384);
    assert(allocation);
    g_texAtlas=(u8 *)(((uintptr_t)allocation+4095)&~(uintptr_t)4095);
    for(i=0;i<NUM_TEXTURES*4096;++i) g_texAtlas[i]=(i%5)?(u8)(next()|1):0;
    for(i=0;i<16;++i) for(j=0;j<256;++j) g_shadeLUT[i][j]=(u8)(j+i);
    for(i=0;i<256;++i) sqTab[i]=i*i;
    for(i=0;i<NUM_TEXTURES;++i) { texAvg[i]=(u8)i; texTransparent[i]=(i&1)?RP_TRANSPARENT:0; }
    for(i=0;i<24000;++i) {
        int dir=i%6,ex=dir<=DIR_PX?1:2+(int)(next()%15);
        int ez=dir<=DIR_PY?2+(int)(next()%15):1;
        g_W=256;g_viewDist=12;g_renderScale=1+(i&1);
        g_textureSubdivision=i%11==0?SUBDIV_FULL:(i%13==0?SUBDIV_OFF:SUBDIV_ADAPTIVE);
        g_flatDist=i%7==0?10:0;g_distanceMode=(i%5==0);
        g_cam=(Camera){(96+(int)(next()%32))*256+(int)(next()%256),
            (8+(int)(next()%(WH-16)))*256+(int)(next()%256),
            (96+(int)(next()%32))*256+(int)(next()%256),
            (int)(next()%1024),(int)(next()%501)-250};
        reference_setup_camera();candidate_setup_camera();
        vw=320/g_renderScale;vh=200/g_renderScale;focal16=172*16/g_renderScale;
        cx16=vw*8;cy16=vh*8;build_inv_table();
        int x=(g_cam.x>>8)-(int)(next()%ex),z=(g_cam.z>>8)-(int)(next()%ez);
        int y=(g_cam.y>>8)+(int)(next()%7)-3,tex=next()%NUM_TEXTURES,light=next()%256;
        skyDarkenCur=next()%12;
        /* Include actual occlusion capture/query fallbacks and fresh reset. */
        occCapture=i%17==0;occQuery=i%19==0;occCurrentKey=i+1;
        unsigned oldCalls=0,oldPixels=0,oldFaces=0;
        for(side=0;side<2;++side) {
            u8 *fb=side?b:a;memset(fb,197,sizeof(a));packet_count[side]=0;
            project_calls=0;g_statFaces=g_statPixels=0;occBudget=16;
            raster_set_target(fb+16,vw,vh,512,g_renderScale);
            raster_occlusion_reset();
            if(side) candidate_draw_quad(x,y,z,dir,ex,ez,tex,light);
            else reference_draw_quad(x,y,z,dir,ex,ez,tex,light);
            raster_finish();
            if(!side) {oldCalls=project_calls;oldPixels=g_statPixels;oldFaces=g_statFaces;}
            else {
                int exact=1;
#ifdef DEPTH_ADAPTIVE_TEST
                exact=g_renderScale==1 || texTransparent[tex] || occCapture || occQuery || g_textureSubdivision!=SUBDIV_ADAPTIVE;
                if(!exact) {
                    qualityChanges+=memcmp(a,b,sizeof(a))!=0;
                    assert(packet_count[1]<=packet_count[0]);
                    for(j=0;j<16;++j) assert(a[j]==197 && b[j]==197 && a[sizeof(a)-1-j]==197 && b[sizeof(a)-1-j]==197);
                }
#endif
                if(exact && (packet_count[0]!=packet_count[1] || memcmp(packets[0],packets[1],packet_count[0]*sizeof(Packet)) ||
                    memcmp(a,b,sizeof(a)) || oldPixels!=g_statPixels || oldFaces!=g_statFaces || project_calls>oldCalls)) {
                    fprintf(stderr,"mismatch parent=%u dir=%d scale=%d packets=%u/%u projection=%u/%u\n",
                        i,dir,g_renderScale,packet_count[0],packet_count[1],oldCalls,project_calls);return 2;
                }
                assert(!splitReuseActive && !splitReuseValid);
                totalOld+=oldCalls;totalNew+=project_calls;savedCases+=project_calls<oldCalls;
            }
        }
    }
#ifdef DEPTH_ADAPTIVE_TEST
    assert(savedCases>0 && totalNew<totalOld && qualityChanges>0);
    printf("PASS: 24000 real adaptive/full/off parents; High/cutout/occlusion/full/off exact, Low opaque bounds preserved; projections %u -> %u, saved cases %u, changed synthetic images %u\n",totalOld,totalNew,savedCases,qualityChanges);
#else
    assert(savedCases>100 && totalNew<totalOld);
    printf("PASS: 24000 real adaptive/full/off parents, six directions, both scales, camera resets, near/guard clipping, cutout/flat/occlusion; packets/images/guards identical; projections %u -> %u, saved cases %u\n",totalOld,totalNew,savedCases);
#endif
    free(allocation);return 0;
}
