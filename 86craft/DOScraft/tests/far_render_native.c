#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "gfx.h"
#include "fmath.h"
#include "textures.h"
#include "far_field.h"
u8 *g_height,*g_blocks;
short *g_columnMap,*g_columnCoords;
int g_residentColumns;
u32 dos_far_boundary_version;
static u8 mockReady[1];
static int readyEnabled;
const u8 *dos_far_ready_flags(void) { return readyEnabled ? mockReady : NULL; }
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
    if((unsigned)x>=1024 || (unsigned)z>=1024) return 0;
    if(detail && readyEnabled) {
        int s=g_columnMap[(z>>4)*64+(x>>4)];
        if(s>=0 && mockReady[s]) {
            int cell=((z&15)<<4)+(x&15),h=g_height[s*256+cell];
            const u8 *p=g_blocks+s*12288+cell*48;
            while(h && !(blk_flags(p[h-1])&BF_CUBE)) --h;
            *b=h ? BLK_ID(p[h-1]) : B_AIR; return h;
        }
    }
    const u8 *p=dos_far_map+((z&~1)<<9)+(x&~1);
    *b=BLK_ID(p[1]); return p[0];
}
#ifndef FAR_RENDER_TEST_INCLUDE
#define FAR_RENDER_TEST_INCLUDE "far_render.inc"
#endif
#include FAR_RENDER_TEST_INCLUDE
#ifndef FAR_TEST_LEVELS
#ifdef FAR_RENDER_TEST_LEGACY
#define FAR_TEST_LEVELS 3
#else
#define FAR_TEST_LEVELS (DOS_FAR_COUNT-1)
#endif
#endif
static unsigned changes(void) { unsigned n=0; for(int i=0;i<512*200;++i) n+=rbuf[i]!=0xee; return n; }
static u32 frame_hash(void) {
    u32 hash=2166136261u;
    for(int y=0;y<200;++y) for(int x=0;x<320;++x) hash=(hash^rbuf[y*512+x])*16777619u;
    return hash;
}
static u8 logical_pixel(int x,int y) {
    return rplanar ? rbuf[y*PLANAR_ROW+(x&1)*PLANAR_C+(x>>1)] : rbuf[y*rpitch*rscale+x*rscale];
}
#ifndef FAR_RENDER_TEST_LEGACY
static void material_patterns(void) {
    /* Real layout writes: quiet surfaces are solid; accents occupy a small
     * minority of pixels, with continuous wood grain and paired rows. */
    for(int layout=0;layout<3;++layout) {
        rplanar=layout==2; rscale=layout ? 2 : 1;
        rw=320/rscale; rh=200/rscale;
        for(int pattern=FAR_PATTERN_SOLID;pattern<=FAR_PATTERN_GRAIN;++pattern) {
            memset(pixels,0xee,sizeof(pixels));
            int width=rscale==1 ? 8 : 4;
            for(int x=0;x<8;x+=width) raster_far_pattern(x,width,0,16,15,14,pattern<<1);
            int accents=0;
            for(int y=0;y<16;++y) for(int x=0;x<8;++x) {
                u8 c=logical_pixel(x,y); assert(c==15 || c==14); accents+=c==14;
                assert(c==logical_pixel(x,y&~1));
            }
            if(pattern==FAR_PATTERN_SOLID) assert(accents==0);
            else assert(accents>0 && accents<=16); /* At most 1/8, versus old 1/2. */
            if(pattern==FAR_PATTERN_LOG) for(int y=2;y<14;++y) assert(logical_pixel(3,y)==14);
            if(pattern==FAR_PATTERN_PLANK) for(int x=0;x<8;++x) assert(logical_pixel(x,4)==14);
            assert(pixels[0]==0xee && logical_pixel(8,0)==0xee && logical_pixel(0,16)==0xee);
        }
    }
    rplanar=0; rscale=g_renderScale=1; vw=rw=320; vh=rh=200;
    dos_far_enabled=DOS_FAR_LOW;
    texAvg[T_GRASS_TOP]=P(R_GRASS,14); texAvg[T_DIRT]=P(R_DIRT,12);
    texAvg[T_LOG_SIDE]=P(R_BARK,10); texAvg[T_LOG_TOP]=P(R_BARK,14);
    RenderEnv day={0},night={0}; night.skyDarken=4;
    for(int fog=0;fog<9;++fog) for(int b=0;b<NUM_BLOCKS;++b) {
        farShade[fog][b]=far_color(b,fog,&day,0);
        farSideShade[fog][b]=far_color(b,fog,&day,1);
    }
    memset(pixels,0xee,sizeof(pixels));
    FarColumn column; raster_far_column(&column,0,8,20);
    far_paint_surface(&column,10,14,B_GRASS,0);
    for(int y=10;y<20;++y) for(int x=0;x<8;++x)
        assert((logical_pixel(x,y)&240)==(y<14 ? R_GRASS : R_DIRT)*16);
    assert(column.top==10 && logical_pixel(0,9)==0xee);
    assert((far_color(B_LOG,0,&day,1)&240)==R_BARK*16);
    assert((far_color(B_GRASS,0,&night,0)&15)<(far_color(B_GRASS,0,&day,0)&15));
    assert(far_pattern(B_GRASS,0)==FAR_PATTERN_SOLID && far_pattern(B_WATER,0)==FAR_PATTERN_SOLID);
    assert((far_ink(B_LEAVES,0,0)>>9)==FAR_PATTERN_LEAVES);
    dos_far_enabled=DOS_FAR_VERY_LOW;
    assert((far_ink(B_LEAVES,0,0)>>9)==FAR_PATTERN_SOLID);
    memset(pixels,0xee,sizeof(pixels)); raster_far_column(&column,0,8,20);
    far_paint_surface(&column,10,14,B_GRASS,0);
    for(int y=10;y<20;++y) assert((logical_pixel(0,y)&240)==R_GRASS*16);
}
#endif
int main(void) {
    dos_far_map=malloc(DOS_FAR_BYTES); assert(dos_far_map);
    for(u32 i=0;i<DOS_FAR_BYTES;i+=2) { dos_far_map[i]=20; dos_far_map[i+1]=B_GRASS; }
    for(int t=0;t<NUM_TEXTURES;++t) texAvg[t]=P(R_GRASS,14);
    for(int l=0;l<16;++l) for(int c=0;c<256;++c) g_shadeLUT[l][c]=(c&240)|l;
    g_cam.x=g_cam.z=512*256; g_cam.y=24*256;
    rw=vw; rh=vh; rpitch=512; rscale=2;
    RenderEnv env={0};
    memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); assert(!changes());
    for(int scale=1;scale<=2;++scale) for(int level=1;level<=FAR_TEST_LEVELS;++level) {
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
    /* Compare the complete far-field image with the checkpoint renderer over
     * varied terrain, materials, camera heights, pitch, yaw and world edges. */
    for(int z=0;z<512;++z) for(int x=0;x<512;++x) {
        int i=(z*512+x)*2;
        dos_far_map[i]=8+((x/3+z/5+(x^z)%7)%32);
        dos_far_map[i+1]=(x+z)%3==0 ? B_SAND : ((x+z)%3==1 ? B_GRASS : B_WATER);
    }
    for(int t=0;t<NUM_TEXTURES;++t) texAvg[t]=P(t%15,14);
    ++dos_far_version; farShadeDarken=-1;
    static const int points[]={0,512,1023};
    for(int scale=1;scale<=2;++scale) for(int level=1;level<=FAR_TEST_LEVELS;++level)
    for(int p=0;p<3;++p) for(int pitch=-80;pitch<=80;pitch+=80)
    for(int yaw=0;yaw<1024;yaw+=128) {
        g_renderScale=rscale=scale; vw=rw=320/scale; vh=rh=200/scale;
        focal16=172*16/scale; cx16=vw*8; dos_far_enabled=level;
        g_cam.x=g_cam.z=points[p]*256; g_cam.y=(p*16+5)*256;
        g_cam.pitch=pitch; g_cam.yaw=yaw;
        memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env);
        /* Drain the intentionally incremental panorama before asserting
         * exact-pose pixel stability of the stationary band cache. */
        while(farPanNext<FAR_PAN_COLS) draw_far_field(&env);
        memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env);
        u32 hash=frame_hash();
        for(int repeat=0;repeat<3;++repeat) {
            memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env);
            assert(hash==frame_hash());
        }
        printf("POSE_HASH=%08x\n",hash);
    }
    /* A ready resident cell can change while its 2x2 far summary stays the
     * same. The edit epoch and resident ownership/readiness must invalidate
     * exact-pose reuse independently of the coarse map version. */
    g_columnMap=malloc(4096*sizeof(short)); g_columnCoords=malloc(2*sizeof(short));
    g_height=malloc(256); g_blocks=calloc(12288,1);
    assert(g_columnMap && g_columnCoords && g_height && g_blocks);
    for(int i=0;i<4096;++i) g_columnMap[i]=-1;
    g_columnMap[32*64+32]=0; g_columnCoords[0]=g_columnCoords[1]=32;
    readyEnabled=1; mockReady[0]=1; g_residentColumns=1;
    for(int i=0;i<256;++i) { g_height[i]=20; g_blocks[i*48+19]=B_STONE; }
    for(u32 i=0;i<DOS_FAR_BYTES;i+=2) { dos_far_map[i]=20; dos_far_map[i+1]=B_GRASS; }
    ++dos_far_version;
    g_cam.x=g_cam.z=512*256; g_cam.y=24*256; g_cam.pitch=g_cam.yaw=0;
    for(int i=0;i<3;++i) { memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); }
    u32 base=frame_hash();
    for(int i=0;i<256;++i) { g_height[i]=48; g_blocks[i*48+47]=B_STONE; }
    ++dos_far_boundary_version;
    memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); assert(frame_hash()!=base);
    mockReady[0]=0;
    memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); u32 coarse=frame_hash();
    mockReady[0]=1;
    memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); assert(frame_hash()!=coarse);
    g_columnMap[32*64+32]=-1; g_columnMap[31*64+32]=0; g_columnCoords[1]=31;
    memset(pixels,0xee,sizeof(pixels)); draw_far_field(&env); assert(frame_hash()==coarse);
    free(g_columnMap); free(g_columnCoords); free(g_height); free(g_blocks);
#ifndef FAR_RENDER_TEST_LEGACY
#ifdef DOS_FAR_DETAILS
    material_patterns();
#endif
#ifdef DOS_FAR_REPROJECT
    /* Half-block eye-height motion must move a cached skyline immediately,
     * without waiting for an angular rebuild or the old one-block threshold. */
    rplanar=0; rscale=g_renderScale=1; vw=rw=320; vh=rh=200;
    dos_far_enabled=1; farPanCount[0]=2;
    farPan[0][0]=(FarRun){20,32,B_GRASS,2};
    farPan[0][1]=(FarRun){20,64,B_GRASS,4};
    int inv[129]; for(int d=20;d<=64;d+=4) inv[d]=(172<<16)/(d*256);
    u32 builds=dos_far_pan_builds; FarColumn col;
    g_cam.y=24*256; far_projection_key(100,16384,172,1,0);
    farPanStamp[0]=0; raster_far_column(&col,0,8,200); far_project_column(&col,0,100,inv); int first=col.top;
    g_cam.y+=128; far_projection_key(100,16384,172,1,0);
    raster_far_column(&col,0,8,200); far_project_column(&col,0,100,inv); assert(col.top>first);
    g_cam.y-=128; far_projection_key(100,16384,172,1,0);
    raster_far_column(&col,0,8,200); far_project_column(&col,0,100,inv); assert(col.top==first);
    assert(dos_far_pan_builds==builds && farPanCount[0]==2);
    /* A previously hidden farther peak is retained and can become visible
     * when eye height changes. Correctness cannot depend on baked occlusion. */
    farPan[0][0]=(FarRun){30,32,B_GRASS,2}; farPan[0][1]=(FarRun){32,64,B_GRASS,4};
    farPanStamp[0]=0;
    g_cam.y=24*256; far_projection_key(100,16384,172,1,0);
    raster_far_column(&col,0,8,200); far_project_column(&col,0,100,inv); first=col.top;
    g_cam.y=30*256; far_projection_key(100,16384,172,1,0);
    raster_far_column(&col,0,8,200); far_project_column(&col,0,100,inv); assert(col.top<100 && col.top>first);
#endif
#endif
    /* The actual staged strip routine must cover both Mode X plane pairs. */
    memset(pixels,0xee,sizeof(pixels)); rplanar=1;
    raster_far_strip(0,4,-20,2,7);
    for(int y=0;y<2;++y) {
        assert(rbuf[y*160]==7 && rbuf[y*160+1]==7);
        assert(rbuf[y*160+PLANAR_C]==7 && rbuf[y*160+PLANAR_C+1]==7);
        assert(rbuf[y*160+2]==0xee);
    }
    assert(rbuf[320]==0xee && pixels[0]==0xee);
    memset(pixels,0xee,sizeof(pixels)); raster_far_pattern(0,4,0,4,7,8,0);
    assert(rbuf[0]==7 && rbuf[1]==7 && rbuf[PLANAR_C]==8 && rbuf[PLANAR_C+1]==8);
    assert(rbuf[160]==7 && rbuf[160+PLANAR_C]==8);
    assert(rbuf[320]==8 && rbuf[320+PLANAR_C]==7);
    rplanar=0; rscale=1; rw=320; rh=200;
    memset(pixels,0xee,sizeof(pixels)); raster_far_strip(312,8,199,220,5);
    for(int x=312;x<320;++x) assert(rbuf[199*512+x]==5);
    assert(rbuf[199*512+320]==0xee && rbuf[200*512+312]==0xee);
    memset(pixels,0xee,sizeof(pixels)); raster_far_pattern(312,8,199,200,7,8,0);
    for(int x=312;x<320;++x) assert(rbuf[199*512+x]==((x&1) ? 7 : 8));
    free(dos_far_map);
    puts("PASS: distant levels, cave/water/pitch gates and planar/chunky strips");
    return 0;
}
