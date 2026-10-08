#define main floor_regression_main
#include "render_occlusion_test.c"
#undef main

static void wall_terrain(int hole)
{
    for(int z=0; z<g_W; ++z) for(int x=0; x<g_W; ++x) for(int y=0; y<WH; ++y)
    {
        u8 b=y<23 ? B_STONE : B_AIR;
        if(z>=35 && z<=36 && x>=24 && x<=40 && y>=23 && y<=31) b=B_DIRT;
        if(z>=40 && z<=44 && x>=25 && x<=42 && y>=23 && y<=33) b=B_STONE;
        if(hole && z>=35 && z<=36 && x>=31 && x<=33 && y>=25 && y<=27)
            b=hole==1 ? B_AIR : B_GLASS;
        g_blocks[widx(x,y,z)]=b; g_light[widx(x,y,z)]=0xF0;
    }
    mesh_reset(); world_stream(32,32,16,100000);
}

int main(void)
{
    floor_regression_main();
    for(int t=0; t<NUM_TEXTURES; ++t) for(int l=0; l<16; ++l)
        for(int v=0; v<16; ++v) for(int u=0; u<16; ++u)
            g_texAtlas[t*4096+v*256+l*16+u]=((t==T_GLASS || t==T_WATER) && u<4) ? 0 : 16+(t*13+l*7+u+v)%220;
    for(int l=0; l<16; ++l) for(int i=0; i<256; ++i) g_shadeLUT[l][i]=16+(l*7+i)%220;
    u8 *fb=calloc(FB_PITCH*SCR_H,1),*reference=calloc(FB_PITCH*SCR_H,1);
    RenderEnv env={0}; env.skyColor=env.fogColor=0x8F; env.underground=1;
    static const int pitches[]={-250,-200,-150,-40,0,160};
    int views=0,culled=0,active=0,maxTiles=0;
    for(int shape=0; shape<9; ++shape)
    {
        if(shape<6) terrain(shape==5 ? 1 : shape); else wall_terrain(shape-6);
        for(int distance=8; distance<=16; distance+=4)
        for(int scale=1; scale<=2; ++scale) for(int pose=0; pose<2; ++pose)
        for(int p=0; p<ARRAY_LEN(pitches); ++p) for(int yaw=0; yaw<1024; yaw+=128)
        {
            g_viewDist=distance; g_flatDist=distance>8 ? 6 : 0;
            g_renderScale=scale; g_distanceMode=pose;
            g_cam.x=32*256+(pose ? 244 : 128); g_cam.z=32*256+128;
            g_cam.y=(shape==5 ? 20 : 26)*256+159; g_cam.yaw=yaw; g_cam.pitch=pitches[p];
            render_clear_boxes();
            for(int hidden=0; hidden<2; ++hidden)
            {
                MBox *b=render_add_box(); b->ox=32*256+128; b->oz=(hidden ? 42 : 34)*256+128;
                b->oy=25*256; b->hw=96; b->h=384;
                b->x0=b->z0=-6; b->x1=b->z1=6; b->y1=24; b->light=15;
                b->yaw=yaw+128; b->pitch=pose ? 64 : 0;
                for(int d=0; d<6; ++d) b->tex[d]=T_STONE;
            }
            ++g_meshVersion; g_occlusion=0; render_frame(reference,&env);
            g_occlusion=1; render_frame(fb,&env); ++views;
            culled+=g_statOccluded; active+=(g_statOcclusionTiles>0);
            maxTiles=MAX(maxTiles,g_statOcclusionTiles);
            for(int y=0; y<VIEW_H; ++y) for(int x=0; x<SCR_W; ++x)
                if(reference[y*FB_PITCH+x]!=fb[y*FB_PITCH+x])
                {
                    printf("FAIL shape=%d distance=%d scale=%d pose=%d pitch=%d yaw=%d pixel=%d,%d before=%d after=%d culled=%u tiles=%u\n",shape,distance,scale,pose,pitches[p],yaw,x,y,reference[y*FB_PITCH+x],fb[y*FB_PITCH+x],g_statOccluded,g_statOcclusionTiles);
                    return 1;
                }
        }
    }
    check(culled>0,"general occlusion actually rejects hidden faces");
    wall_terrain(0); g_viewDist=12; g_flatDist=6; g_distanceMode=1; g_renderScale=2;
    render_clear_boxes();
    u8 *sequence=calloc(8,FB_PITCH*SCR_H);
    for(int enabled=0; enabled<2; ++enabled)
    {
        g_occlusion=enabled;
        for(int frame=0; frame<8; ++frame)
        {
            g_cam.x=32*256+128+(frame/2)*40; g_cam.z=32*256+128;
            g_cam.y=26*256+159; g_cam.yaw=(frame/2)*7; g_cam.pitch=0;
            render_frame(fb,&env);
            if(!enabled) memcpy(sequence+frame*FB_PITCH*SCR_H,fb,FB_PITCH*SCR_H);
            else check(!memcmp(sequence+frame*FB_PITCH*SCR_H,fb,FB_PITCH*SCR_H),"exact-pose cache and small camera movements match");
        }
    }
    wall_terrain(1); render_frame(fb,&env); g_occlusion=0; render_frame(reference,&env);
    check(!memcmp(fb,reference,FB_PITCH*SCR_H),"opening a wall invalidates occlusion proofs and cached hidden items");
    printf("PASS %d general occlusion views match; %d faces rejected, %d active views, maximum %d tiles\n",views,culled,active,maxTiles);
    return 0;
}
