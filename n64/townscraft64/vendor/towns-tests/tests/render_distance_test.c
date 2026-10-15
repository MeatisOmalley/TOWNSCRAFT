/* Compare offscreen subdivision culling against the original draw path.
   Constant-color opaque tiles and patterned transparent tiles expose gaps,
   changed coverage and incorrect rejection independently of texture warp. */
#define main occlusion_regression_main
#include "render_occlusion_test.c"
#undef main

int main(void)
{
    occlusion_regression_main();
    u8 *fb=calloc(FB_PITCH*SCR_H,1),*reference=calloc(FB_PITCH*SCR_H,1);
    RenderEnv env={0}; env.skyColor=env.fogColor=0x8F; env.underground=1;
    static const int pitches[]={-250,-200,-150,-40,0,160};
    int views=0,different=0,missing=0;
    for(int opening=0; opening<5; ++opening)
    {
        terrain(opening);
        for(int distance=12; distance<=16; distance+=4)
        for(int scale=1; scale<=2; ++scale) for(int pose=0; pose<2; ++pose)
        for(int p=0; p<ARRAY_LEN(pitches); ++p) for(int yaw=0; yaw<1024; yaw+=128)
        {
            g_viewDist=distance; g_flatDist=6; g_renderScale=scale;
            g_cam.x=32*256+(pose ? 244 : 128); g_cam.z=32*256+128;
            g_cam.y=26*256+159; g_cam.yaw=yaw; g_cam.pitch=pitches[p];
            render_clear_boxes(); g_distanceMode=0; ++g_meshVersion;
            render_frame(reference,&env); g_distanceMode=1; render_frame(fb,&env); ++views;
            for(int y=0; y<VIEW_H; ++y) for(int x=0; x<SCR_W; ++x)
            {
                u8 before=reference[y*FB_PITCH+x],after=fb[y*FB_PITCH+x];
                if(before!=after)
                {
                    if(!different) printf("first difference opening=%d distance=%d scale=%d pose=%d pitch=%d yaw=%d x=%d y=%d before=%d after=%d\n",opening,distance,scale,pose,pitches[p],yaw,x,y,before,after);
                    ++different; if(before==42 && after==0x8F) ++missing;
                }
            }
        }
    }
    printf("%s: %d distance views; %d changed pixels, %d newly uncovered pixels\n",different ? "FAIL" : "PASS",views,different,missing);
    return different ? 1 : 0;
}
