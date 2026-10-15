#define main floor_regression_main
#include "render_occlusion_test.c"
#undef main

int main(void)
{
    check(g_textureSubdivision==SUBDIV_ADAPTIVE,"default subdivision stays adaptive");
    floor_regression_main();
    terrain(0); render_clear_boxes();
    g_cam.x=g_cam.z=32*256+128; g_cam.y=26*256+159;
    g_cam.yaw=0; g_cam.pitch=-80; g_renderScale=2;
    u8 *fb=calloc(FB_PITCH*SCR_H,1),*reference=calloc(SUBDIV_COUNT,FB_PITCH*SCR_H);
    RenderEnv env={0}; env.skyColor=env.fogColor=0x8F; env.underground=1;
    for(int t=0;t<NUM_TEXTURES;++t) for(int l=0;l<16;++l)
        for(int v=0;v<16;++v) for(int u=0;u<16;++u)
            g_texAtlas[t*4096+v*256+l*16+u]=16+(u*11+v*7+l)%220;
    u32 faces[SUBDIV_COUNT];
    for(int mode=0;mode<SUBDIV_COUNT;++mode)
    {
        g_textureSubdivision=mode; g_occlusion=0;
        render_frame(reference+mode*FB_PITCH*SCR_H,&env);
        faces[mode]=g_statFaces;
    }
    g_occlusion=1;
    for(int step=0;step<=SUBDIV_COUNT;++step)
    {
        int mode=step%SUBDIV_COUNT;
        g_textureSubdivision=mode;
        /* Keep meshes/camera fixed while changing modes: the proof map and
           per-item hidden flags must follow the new rasterized patches. */
        render_frame(fb,&env);
        check(!memcmp(fb,reference+mode*FB_PITCH*SCR_H,FB_PITCH*SCR_H),"mode switch refreshes occlusion proofs");
        render_frame(fb,&env);
        check(!memcmp(fb,reference+mode*FB_PITCH*SCR_H,FB_PITCH*SCR_H),"reused proofs match new mode");
    }
    printf("Subdivision face counts full/adaptive/off: %u/%u/%u\n",
           faces[SUBDIV_FULL],faces[SUBDIV_ADAPTIVE],faces[SUBDIV_OFF]);
    check(faces[SUBDIV_FULL]>faces[SUBDIV_ADAPTIVE],"full subdivision draws more pieces");
    check(faces[SUBDIV_ADAPTIVE]>faces[SUBDIV_OFF],"off avoids near texture pieces");
    printf("PASS: full/adaptive/off draw %u/%u/%u faces; fixed-pose switching and wrap preserve occlusion correctness\n",
           faces[SUBDIV_FULL],faces[SUBDIV_ADAPTIVE],faces[SUBDIV_OFF]);
    return 0;
}
