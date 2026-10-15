#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "viewmodel.h"
#include "raster.h"
#include "render.h"
#include "textures.h"
#include "graphics_presets.h"
void raster_set_odd_copy_policy(int,int,int,int,int);
void raster_complete_odd_rows(void);
int textures_set_resolution(int);
static u8 palette[768],frame[512*240+32],planes[76800];
int g_renderScale=1;
int dos_cpu386;
u32 g_statFaces;
void video_set_palette(int i,int r,int g,int b) { palette[i*3]=r;palette[i*3+1]=g;palette[i*3+2]=b; }
void *heap_alloc_low(u32 n) { void *p=calloc(1,n);assert(p);return p; }
unsigned char *dos_video_planar_target(void) { return planes; }
void dos_video_planar_frame(const unsigned char *rows) { (void)rows; }
#include "texture_hash.inc"
static int draw(int resolution,int item,int light,int tick,int moving,int planar)
{
    memset(frame,0xee,sizeof(frame));memset(planes,4,sizeof(planes));
    g_renderScale=dos_resolution_set(resolution);textures_set_resolution(g_renderScale);
    /* Debug band disables planar; the other case exercises production planar. */
    raster_set_odd_copy_policy(1,!planar,0,0,0);
    raster_set_target(frame+16,320/g_renderScale,200/g_renderScale,512,g_renderScale);
    raster_fill_rows(0,200/g_renderScale,4);g_statFaces=0;
    dos_viewmodel_update(1,item,light,tick<30 ? 5 : 0,0,0,0,0,100);
    dos_viewmodel_update(1,item,light,tick<30 ? 5 : 0,0,moving,moving?tick*32:0,0,100+tick);
    dos_viewmodel_draw(320/g_renderScale,200/g_renderScale);
    raster_finish();raster_complete_odd_rows();
    for(int i=0;i<16;++i) assert(frame[i]==0xee);
    for(int i=16+512*200;i<(int)sizeof(frame);++i) assert(frame[i]==0xee);
    for(int y=0;y<200;++y) for(int x=320;x<512;++x) assert(frame[16+y*512+x]==0xee);
    int pixels=0;
    for(int y=0;y<200;++y) for(int x=0;x<320;++x) pixels+=frame[16+y*512+x]!=4;
    if(!(pixels>0 && pixels<50000)) fprintf(stderr,"r=%d item=%d tick=%d planar=%d pixels=%d faces=%u\n",resolution,item,tick,planar,pixels,g_statFaces);
    assert(pixels>0 && pixels<50000);assert(g_statFaces>=1 && g_statFaces<=4);
    return pixels;
}
static u32 frame_hash(void)
{
    u32 h=2166136261u;
    for(int y=0;y<200;++y) for(int x=0;x<320;++x) h=(h^frame[16+y*512+x])*16777619u;
    return h;
}
int main(int argc,char **argv)
{
    items_init();textures_init();palette_apply();
    for(int r=0;r<DOS_RES_COUNT;++r) for(int p=0;p<2;++p)
        for(int item=0;item<NUM_ITEMS;++item) for(int tick=0;tick<=30;tick+=5) {
            dos_viewmodel_update(0,0,0,0,0,0,0,0,0);
            draw(r,item,tick&1 ? 0 : 15,tick,tick&1,p);
            if(item==B_COBBLE) assert(g_statFaces<=3);
            if(item==B_TORCH || item==I_IRON_PICK) assert(g_statFaces==1);
        }
    dos_viewmodel_update(0,0,0,0,0,0,0,0,0);
    draw(DOS_RES_FULL,I_IRON_PICK,15,30,0,0);u32 resting=frame_hash();
    dos_viewmodel_update(0,0,0,0,0,0,0,0,0);
    draw(DOS_RES_FULL,I_IRON_PICK,15,15,0,0);assert(frame_hash()!=resting);
    dos_viewmodel_update(0,0,0,0,0,0,0,0,0);
    draw(DOS_RES_FULL,I_IRON_PICK,15,30,1,0);assert(frame_hash()!=resting);
    if(argc==2) {
        const int items[]={0,B_GRASS,I_IRON_PICK,B_TORCH};
        const char *names[]={"HAND","BLOCK","TOOL","TORCH"};
        for(int r=0;r<2;++r) for(int i=0;i<4;++i) {
            dos_viewmodel_update(0,0,0,0,0,0,0,0,0);
            draw(r,items[i],15,30,0,0);
            char path[2048];snprintf(path,sizeof(path),"%s/%s%d.RAW",argv[1],names[i],r);
            FILE *f=fopen(path,"wb");assert(f);fwrite(palette,1,768,f);
            for(int y=0;y<240;++y) fwrite(frame+16+y*512,1,320,f);assert(!fclose(f));
        }
    }
    dos_viewmodel_update(0,0,0,0,0,0,0,0,0);g_statFaces=0;dos_viewmodel_draw(160,100);assert(!g_statFaces);
    puts("PASS: real viewmodel/raster bounds, all items, four resolutions, planar/chunky, light, action and hidden state");
    return 0;
}
