#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "textures.h"
static unsigned char palette[768];
int g_renderScale=1;
static int allocations;
void video_set_palette(int i,int r,int g,int b) { palette[i*3]=r; palette[i*3+1]=g; palette[i*3+2]=b; }
void *heap_alloc_low(u32 n) { void *p=calloc(1,n); assert(p); ++allocations; return p; }
#include "texture_hash.inc"
#ifdef DOS_TEXTURE_CHECK
int dos_texture_test_grass_patch(int x,int y);
/* Compile the real renderer metadata refresh and distant cache invalidation,
 * without linking the unrelated world/raster/emulator subsystems. */
#define DOS_FAR_FIELD
#define DOS_FAR_REPROJECT
static u8 texTransparent[NUM_TEXTURES],texAvg[NUM_TEXTURES];
static int farShadeDarken,farShadeDetail,farBandValid;
static u32 farProjectionSerial=1;
#include "texture_refresh.inc"
#include "texture_far_refresh.inc"
static void verify_atlas(void)
{
    for(int t=0;t<NUM_SHADED_TEXTURES;++t) for(int L=0;L<16;++L) for(int i=0;i<256;++i)
        assert(g_texAtlas[t*4096+(i>>4)*256+L*16+(i&15)]==g_shadeLUT[L][g_tex[t][i]]);
}
static void write_art(const char *path)
{
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fwrite(palette,1,sizeof(palette),f)==sizeof(palette));
    assert(fwrite(g_tex,1,sizeof(g_tex),f)==sizeof(g_tex)); assert(!fclose(f));
}
#endif
int main(int argc,char **argv)
{
    textures_init(); palette_apply();
#ifdef DOS_TEXTURE_CHECK
    int dark=0,light=0;
    for(int i=0;i<256;++i) {
        assert((g_tex[T_GRASS_TOP][i]>>4)==R_GRASS);
        assert((g_tex[T_GRASS_TOP][i]&15)>=11 && (g_tex[T_GRASS_TOP][i]&15)<=13);
        dark+=g_tex[T_GRASS_TOP][i]==P(R_GRASS,11);
        light+=g_tex[T_GRASS_TOP][i]==P(R_GRASS,13);
        assert((g_tex[T_DIRT][i]>>4)==R_DIRT);
        assert((g_tex[T_LEAVES][i]>>4)==R_LEAF && g_tex[T_LEAVES][i]!=0);
        assert((g_tex[T_SAND][i]>>4)==R_SAND);
        assert((g_tex[T_WATER][i]>>4)==R_WATER);
    }
    assert(dark>0 && dark<40 && light>32);
    for(int y=-16;y<32;++y) for(int x=-16;x<32;++x) {
        int p=dos_texture_test_grass_patch(x,y);
        assert(p==dos_texture_test_grass_patch(x+16,y));
        assert(p==dos_texture_test_grass_patch(x,y+16));
    }
    verify_atlas();
    /* Model UV crops must not accidentally punch holes in solid mob bodies. */
    for(int t=T_PIG_SKIN;t<=T_PLAYER_SKIN;++t)
        for(int i=0;i<256;++i) assert(g_tex[t][i]!=0);
    static u8 detailed[NUM_TEXTURES][256],simple[NUM_TEXTURES][256];
    memcpy(detailed,g_tex,sizeof(g_tex)); u8 *atlas=g_texAtlas;
    assert(allocations==1);
    g_renderScale=2; render_sync_texture_resolution();
    memcpy(simple,g_tex,sizeof(g_tex));
    assert(memcmp(simple[T_GRASS_TOP],detailed[T_GRASS_TOP],256));
    assert(memcmp(simple[T_STONE],detailed[T_STONE],256));
    assert(memcmp(simple[T_LEAVES],detailed[T_LEAVES],256));
    assert(memcmp(simple[T_COBBLE],detailed[T_COBBLE],256));
    assert(memcmp(simple[T_SAND],detailed[T_SAND],256));
    assert(memcmp(simple[T_GRAVEL],detailed[T_GRAVEL],256));
    int sandBase=0;
    for(int i=0;i<256;++i) {
        sandBase+=g_tex[T_SAND][i]==P(R_SAND,15);
        assert(g_tex[T_SAND][i]==P(R_SAND,14) || g_tex[T_SAND][i]==P(R_SAND,15));
        assert(g_tex[T_STONE][i]>=P(R_GRAY,10) && g_tex[T_STONE][i]<=P(R_GRAY,12));
    }
    assert(sandBase>=230);
    if(argc==2) {
        char path[2048]; const char *slash=strrchr(argv[1],'\\');
        if(!slash) slash=strrchr(argv[1],'/'); assert(slash);
        int length=(int)(slash-argv[1])+1; assert(length+11<(int)sizeof(path));
        memcpy(path,argv[1],length); strcpy(path+length,"simple.bin"); write_art(path);
    }
    for(int cycle=0;cycle<12;++cycle) {
        int scale=(cycle&1) ? 2 : 1;
        g_renderScale=scale;
        farShadeDarken=4; farShadeDetail=1; farBandValid=1;
        u32 serial=farProjectionSerial;
        render_sync_texture_resolution();
        assert(farShadeDarken==-1 && farShadeDetail==-1 && !farBandValid);
        assert(farProjectionSerial==serial+1);
        assert(!memcmp(g_tex,scale==1 ? detailed : simple,sizeof(g_tex)));
        for(int t=0;t<NUM_TEXTURES;++t) {
            int transparent=0;
            for(int i=0;i<256;++i) {
                transparent|=g_tex[t][i]==0;
            }
            assert(texAvg[t]==texture_average(t) && texTransparent[t]==transparent);
        }
        assert(!texTransparent[T_LEAVES]);
        for(int i=0;i<256;++i) assert(g_tex[T_LEAVES][i]!=0);
        assert((blk_flags(B_LEAVES)&(BF_OPAQUE|BF_CUBE))==(BF_OPAQUE|BF_CUBE));
        assert(blk_flags(B_LEAVES|64)==blk_flags(B_LEAVES));
        verify_atlas(); assert(atlas==g_texAtlas && allocations==1);
        serial=farProjectionSerial; render_sync_texture_resolution();
        assert(serial==farProjectionSerial); /* Same-resolution settings do nothing. */
    }
    g_renderScale=2; textures_init();
    assert(!memcmp(g_tex,simple,sizeof(g_tex)) && atlas==g_texAtlas && allocations==1);
    g_renderScale=1; render_sync_texture_resolution();
#endif
    if(argc==2) {
        FILE *f=fopen(argv[1],"wb"); assert(f);
        assert(fwrite(palette,1,sizeof(palette),f)==sizeof(palette));
        assert(fwrite(g_tex,1,sizeof(g_tex),f)==sizeof(g_tex)); assert(!fclose(f));
    }
#ifdef DOS_TEXTURE_CHECK
    puts("PASS: texture resolution switching, atlas reuse, flat colors and distant cache refresh");
#else
    puts("PASS: original texture reference generated");
#endif
    return 0;
}
