#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "gfx.h"
#include "fmath.h"
#include "textures.h"
#include "far_field.h"
#define PLANAR_C 38400
#define PLANAR_ROW 160
static int rw,rh,rpitch,rscale,rplanar;
static u8 pixels[512*240+32],*rbuf=pixels+16;
#include "far_raster_test.inc"
Camera g_cam;
int g_viewDist=8,g_renderScale=2,dos_far_enabled;
u32 dos_far_version=1;
u8 *dos_far_map;
u8 g_shadeLUT[16][256];
static u8 texAvg[NUM_TEXTURES];
static int vw=160,vh=100,focal16=86*16,cx16=160*8;
int dos_far_sample(int x,int z,int detail,u8 *b) {
    (void)detail;
    if((unsigned)x>=1024 || (unsigned)z>=1024) return 0;
    const u8 *p=dos_far_map+((z&~1)<<9)+(x&~1);
    *b=p[1]; return p[0];
}
#include "far_render.inc"
static unsigned changes(void) { unsigned n=0; for(int i=0;i<512*200;++i) n+=rbuf[i]!=0xee; return n; }
int main(void) {
    dos_far_map=malloc(DOS_FAR_BYTES); assert(dos_far_map);
    for(u32 i=0;i<DOS_FAR_BYTES;i+=2) { dos_far_map[i]=20; dos_far_map[i+1]=B_GRASS; }
    for(int t=0;t<NUM_TEXTURES;++t) texAvg[t]=P(R_GRASS,14);
    for(int l=0;l<16;++l) for(int c=0;c<256;++c) g_shadeLUT[l][c]=(c&240)|l;
    g_cam.x=g_cam.z=512*256; g_cam.y=24*256;
    rw=vw; rh=vh; rpitch=512; rscale=2;
    RenderEnv env={0};
    memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); assert(!changes());
    for(int scale=1;scale<=2;++scale) for(int level=1;level<=3;++level) {
        g_renderScale=rscale=scale; vw=rw=320/scale; vh=rh=200/scale;
        focal16=172*16/scale; cx16=vw*8;
        dos_far_enabled=level;
        memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env);
        assert(changes()>0 && rbuf[(vh-1)*rscale*512]!=0xee && rbuf[0]==0xee);
        env.underground=1; memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); assert(!changes());
        env.underground=0; env.underwater=1; draw_far_field(&env); assert(!changes());
        env.underwater=0; g_cam.pitch=250; draw_far_field(&env); assert(!changes());
        g_cam.pitch=-250; draw_far_field(&env); assert(!changes()); g_cam.pitch=0;
        g_viewDist=16; draw_far_field(&env); assert(changes()>0); g_viewDist=8;
    }
    /* The actual staged strip routine must cover both Mode X plane pairs. */
    memset(pixels,0xee,sizeof(pixels)); rplanar=1;
    raster_far_strip(0,4,-20,2,7);
    for(int y=0;y<2;++y) {
        assert(rbuf[y*160]==7 && rbuf[y*160+1]==7);
        assert(rbuf[y*160+PLANAR_C]==7 && rbuf[y*160+PLANAR_C+1]==7);
        assert(rbuf[y*160+2]==0xee);
    }
    assert(rbuf[320]==0xee && pixels[0]==0xee);
    rplanar=0; rscale=1; rw=320; rh=200;
    memset(pixels,0xee,sizeof(pixels)); raster_far_strip(312,8,199,220,5);
    for(int x=312;x<320;++x) assert(rbuf[199*512+x]==5);
    assert(rbuf[199*512+320]==0xee && rbuf[200*512+312]==0xee);
    free(dos_far_map);
    puts("PASS: distant levels, cave/water/pitch gates and planar/chunky strips");
    return 0;
}
