/* Native 32-bit downward-occlusion regression. Link with the real raster/trap
   loops. Compare every viewport pixel with the same renderer with the floor
   shortcut disabled, including transparent openings and entity boxes. */
#include "../src/world.c"
#include "../src/render.c"

extern int printf(const char *,...);
extern void *calloc(unsigned int,unsigned int);
extern void exit(int);
u32 g_ramMB=2;
volatile u32 g_ticks;
u8 g_tex[NUM_TEXTURES][256],*g_texAtlas,g_shadeLUT[16][256];
u32 heap_high_free(void) { return 1024*1024; }
u32 heap_low_free(void) { return 200000; }
void *heap_alloc_low(u32 n) { return calloc(1,n); }
void *heap_alloc_high(u32 n) { return calloc(1,n); }
u32 heap_high_mark(void) { return 0; }
void heap_high_rewind(u32 mark) {}
void fatal(const char *message) { printf("FATAL: %s\n",message); exit(1); }
void gfx_wait_flip(void) {}
u8 texture_average(int texture) { return 42; }
static void check(int ok,const char *message) { if(!ok) { printf("FAIL: %s\n",message); exit(1); } }

static void terrain(int opening)
{
    for(int z=0; z<g_W; ++z) for(int x=0; x<g_W; ++x) for(int y=0; y<WH; ++y)
    {
        int cave=x>=28 && x<=36 && z>=28 && z<=36 && y>=17 && y<=22;
        u8 b=y<25 && !cave ? B_STONE : B_AIR;
        if(x==32 && z==32 && y>=23 && y<=24)
            b=opening==1 ? B_AIR : opening==2 ? B_GLASS : opening==3 ? B_WATER : b;
        if(opening==4 && x>=33 && y>=23) b=B_AIR;
        g_blocks[widx(x,y,z)]=b; g_light[widx(x,y,z)]=0xF0;
    }
    mesh_reset(); world_stream(32,32,12,100000);
}

int main(void)
{
#ifdef TEST_SUBDIVISION
    g_textureSubdivision=TEST_SUBDIVISION;
#endif
    cacheAllocated=0; g_columnMap=NULL; g_W=64; g_NC=4; strideZ=g_W*WH;
    g_allocChunkCount=g_NC*g_NC*NCY;
    g_blocks=calloc(g_W*g_W*WH,1); g_light=calloc(g_W*g_W*WH,1);
    g_height=calloc(g_W*g_W,1); g_zOff=calloc(g_W,4);
    for(int z=0; z<g_W; ++z) g_zOff[z]=z*strideZ;
    g_chunks=calloc(g_allocChunkCount,sizeof(Chunk));
    poolSize=16000; g_meshPool=calloc(poolSize,8); init_hides_tab();
    memset(g_tex,42,sizeof(g_tex)); memset(g_shadeLUT,42,sizeof(g_shadeLUT));
    for(int i=0; i<256; ++i) if((i&15)<4) g_tex[T_GLASS][i]=g_tex[T_WATER][i]=0;
    g_texAtlas=(u8 *)(((u32)calloc(NUM_TEXTURES*4096+4095,1)+4095)&~4095u);
    memset(g_texAtlas,42,NUM_TEXTURES*4096);
    for(int i=0; i<4096; ++i) if((i&15)<4) g_texAtlas[T_GLASS*4096+i]=g_texAtlas[T_WATER*4096+i]=0;
    render_init(); g_viewDist=8; g_flatLOD=0;
    u8 *fb=calloc(FB_PITCH*SCR_H,1),*reference=calloc(FB_PITCH*SCR_H,1);
    RenderEnv env={0}; env.skyColor=env.fogColor=0x8F; env.underground=1;
    int views=0;
    static const int pitches[]={-250,-200,-150,-40,0,160};
    for(int opening=0; opening<5; ++opening)
    {
        terrain(opening);
        for(int scale=1; scale<=2; ++scale) for(int pose=0; pose<2; ++pose)
        for(int p=0; p<ARRAY_LEN(pitches); ++p) for(int yaw=0; yaw<1024; yaw+=128)
        {
            int pitch=pitches[p];
            g_renderScale=scale; g_cam.x=32*256+(pose ? 244 : 128);
            g_cam.z=32*256+128; g_cam.y=26*256+159;
            g_cam.yaw=yaw; g_cam.pitch=pitch;
            render_clear_boxes();
            for(int hidden=0; hidden<2; ++hidden)
            {
                MBox *b=render_add_box(); b->ox=32*256+128; b->oz=32*256+128;
                b->oy=(hidden ? 20 : 25)*256; b->hw=96; b->h=384;
                b->x0=b->z0=-6; b->x1=b->z1=6; b->y1=24; b->light=15;
                for(int d=0; d<6; ++d) b->tex[d]=T_STONE;
            }
            render_frame(fb,&env); ++views;
            if(!opening && pitch<=-160) check(floorOcclusion==25,"opaque floor is recognized");
            if(opening>0 && opening<4 && floorOcclusion)
            {
                printf("unexpected floor opening=%d scale=%d pose=%d pitch=%d yaw=%d floor=%d\n",opening,scale,pose,pitch,yaw,floorOcclusion);
                check(0,"cave opening and transparent floors stay visible");
            }
            /* Camera and mesh are unchanged, so the proof cache will preserve
               this override while the collected list rebuilds for floor=0. */
            floorOcclusion=0; render_frame(reference,&env);
            for(int y=0; y<VIEW_H; ++y)
            {
                if(memcmp(reference+y*FB_PITCH,fb+y*FB_PITCH,SCR_W))
                {
                    printf("FAIL: pixels differ opening=%d scale=%d pose=%d pitch=%d yaw=%d row=%d\n",opening,scale,pose,pitch,yaw,y);
                    return 1;
                }
            }
        }
    }
    terrain(0); g_cam.pitch=-250; render_frame(fb,&env);
    Chunk *c=&g_chunks[chunk_index(2,1,2)];
    c->meshed=0; check(!find_floor_occlusion(),"unfinished floor mesh does not occlude");
    c->meshed=1; c->dirty=DIRTY_GEOMETRY;
    check(!find_floor_occlusion(),"pending floor edit does not occlude"); c->dirty=0;
    g_cam.pitch=-40; setup_camera(); check(!find_floor_occlusion(),"forward view has no floor shortcut");
    short missing[16]; for(int i=0; i<16; ++i) missing[i]=-1;
    g_cam.pitch=-250; setup_camera(); g_columnMap=missing;
    check(!find_floor_occlusion(),"unloaded terrain does not stand in for an opaque floor"); g_columnMap=NULL;
    printf("PASS: %d views match uncullled pixels; opaque floors, cliffs, cave openings, transparent tiles, mesh readiness and entities\n",views);
    return 0;
}
