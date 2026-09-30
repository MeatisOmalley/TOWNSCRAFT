#include "common.h"
#include "sys.h"
#include "gfx.h"
#include "video.h"
#include "textures.h"
#include "world.h"
#include "render.h"
#include "raster.h"

u32 g_benchFreqMHz=16;
void run_bench(void);

void kmain(void)
{
	char buf[64];
	RenderEnv env;
	u32 frames=0,t0,fps10=0;
	video_init();
	gfx_init();
	sys_init();
	items_init();
	textures_init();
	palette_apply();
	gfx_clear(g_fb,C_BLACK);
	gfx_text_center(g_fb,110,"Generating world...",C_WHITE,C_BLACK);
	gfx_present(g_fb);
	world_alloc();
	world_generate(12345);
	render_init();

	g_cam.x=g_spawnX*256+128;
	{
		int dx,dz,mh=0;
		for(dz=-2; dz<=2; ++dz) for(dx=-2; dx<=2; ++dx)
		{
			int h=g_height[(g_spawnZ+dz)*g_W+g_spawnX+dx];
			if(h>mh) mh=h;
		}
		g_cam.y=(mh+3)*256;
	}
	g_cam.z=g_spawnZ*256+128;
#ifndef TEST_YAW
#define TEST_YAW 100
#endif
	g_cam.yaw=TEST_YAW;
	g_cam.pitch=-90;
	memset(&env,0,sizeof(env));
	env.skyColor=P(R_SKY,14);
	env.fogColor=P(R_SKY,15);
	env.sunAngle=200;
	t0=g_ticks;
	{
		extern int g_profEnable;
		g_profEnable=1;
	}
	for(;;)
	{
		render_frame(g_fb,&env);
		++frames;
		if(g_ticks-t0>=200)
		{
			fps10=frames*1000/(g_ticks-t0);
			frames=0;
			t0=g_ticks;
		}
		gfx_text_shadow(g_fb,2,2,"FPSx10",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,58,2,itoa_dec(fps10,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,2,12,"W",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,18,12,itoa_dec(g_W,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,50,12,"faces",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,98,12,itoa_dec(g_statFaces,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,140,12,itoa_dec(g_statItems,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,200,12,itoa_dec(g_statPixels,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,200,22,itoa_dec(g_meshQuads,buf),C_WHITE,C_BLACK);
		g_statPixels=0;
		gfx_text_shadow(g_fb,2,22,"lowfree",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,66,22,itoa_dec(heap_low_free()/1024,buf),C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,110,22,"highfree",C_WHITE,C_BLACK);
		gfx_text_shadow(g_fb,182,22,itoa_dec(heap_high_free()/1024,buf),C_WHITE,C_BLACK);
		{
			int k;
			for(k=0; k<5; ++k)
			{
				gfx_text_shadow(g_fb,2+k*40,32,itoa_dec(g_prof[k],buf),C_YELLOW,C_BLACK);
			}
		}
		{u32 t=g_ticks;
		gfx_present(g_fb);
		g_prof[4]+=g_ticks-t;}
		if(g_keyDown[KEY_LEFT]) g_cam.yaw-=8;
		if(g_keyDown[KEY_RIGHT]) g_cam.yaw+=8;
		if(g_keyDown[KEY_UP]) g_cam.pitch+=8;
		if(g_keyDown[KEY_DOWN]) g_cam.pitch-=8;
		if(g_keyDown[KEY_R]) g_renderScale=3-g_renderScale;
	}
}
