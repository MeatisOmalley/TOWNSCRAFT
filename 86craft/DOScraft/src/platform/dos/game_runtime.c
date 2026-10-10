/* Link-only presentation wrapper. Normal play goes straight to the unchanged
 * platform adapter. /SMOKE drives only real AT input and records diagnostics;
 * no source gameplay/render rewrite, test-world substitution or reduced budget. */
#include "sys.h"
#include "world.h"
#include "player.h"
#include "inventory.h"
#include "render.h"
#include "gfx.h"
#include "system.h"
#include "save_backend.h"
#include "hdd_backend.h"
#include "video_backend.h"
#include "save.h"
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int smoke,load_smoke,stage,frames,input_ok=1;
static unsigned int until,start;
static int x0,z0,pitch0,walk_ok,look_ok;
static int save_busy,save_result=-1,load_result=-1;
static int placed,broken,torch_before,torch_after,vga_ok=1;
static int rendered_frames;
void __real_gfx_present(void);
void __real_render_frame(u8 *fb,const RenderEnv *env);
void __wrap_render_frame(u8 *fb,const RenderEnv *env)
{
    __real_render_frame(fb,env);
    if(smoke) ++rendered_frames;
}
int __real_save_world(void);
int __real_load_world(void);
int __wrap_save_world(void)
{
    int result;
    save_busy=1; result=__real_save_world(); save_busy=0;
    if(smoke) save_result=result;
    return result;
}
int __wrap_load_world(void)
{
    int result;
    save_busy=1; result=__real_load_world(); save_busy=0;
    if(smoke) load_result=result;
    return result;
}
static int torches(void)
{
    int i,count=0;
    for(i=0;i<g_residentColumns*COLUMN_CELLS;++i)
        if(BLK_ID(g_blocks[i])==B_TORCH) ++count;
    return count;
}
static void snapshot(const char *path,const u8 *frame)
{
    unsigned char palette[768];
    int i,row,ok=1;
    if(dos_video_verify(frame,512)) vga_ok=0;
    outportb(0x3c7,0);
    for(i=0;i<768;++i) palette[i]=(inportb(0x3c9)&63)*255/63;
    FILE *file=fopen(path,"wb");
    if(!file) { vga_ok=0; return; }
    if(fwrite(palette,1,768,file)!=768) ok=0;
    for(row=0;row<240;++row)
        if(fwrite(frame+row*512,1,320,file)!=320) ok=0;
    if(fclose(file)) ok=0;
    if(!ok) vga_ok=0;
}
static int controller_ready(void)
{
    unsigned int guard=100000;
    while((inportb(0x64)&2) && --guard) {}
    return guard!=0;
}
static void key(unsigned char byte)
{
    if(!controller_ready()) { input_ok=0; return; }
    outportb(0x64,0xd2);
    if(!controller_ready()) { input_ok=0; return; }
    outportb(0x60,byte);
    /* Ensure a complete make/break before injecting another byte. */
    unsigned int guard=100000;
    while((inportb(0x64)&1) && --guard) {}
    if(!guard) input_ok=0;
}
static void tap(unsigned char byte) { key(byte); key(byte|128); }
void dos_game_options(int argc,char **argv)
{
    if(argc==2 && (!strcmp(argv[1],"/SMOKE") || !strcmp(argv[1],"/SMOKELOAD"))) {
        smoke=1; load_smoke=!strcmp(argv[1],"/SMOKELOAD");
    }
}
static void finish(void)
{
    int stream_ok=world_stream_error()==0;
    int width=g_W,columns=g_residentColumns;
    unsigned int mesh=g_meshQuads,ticks=g_ticks-start;
    /* A completed diagnostic exits cleanly, never kill a live save session. */
    dos_save_shutdown(); dos_hdd_shutdown();
    dos_system_shutdown(); dos_video_restore();
    FILE *file=fopen(load_smoke ? "C:\\PLAYLOAD.TXT" : "C:\\PLAYNEW.TXT","wb");
    int storage_ok=load_smoke ? load_result==SAVE_OK : save_result==SAVE_OK;
    int ok=input_ok && walk_ok && look_ok && stream_ok && width==256 && mesh>0 &&
        storage_ok && placed && broken && vga_ok;
    if(!file) exit(1);
    int written=fprintf(file,"DOScraft real game-loop smoke\nMODE=%s\nWORLD_WIDTH=%d\n"
        "RESIDENT_COLUMNS=%d\nMESH_QUADS=%u\nPRESENTED_FRAMES=%d\nAT_INPUT=%s\n"
        "WALK_MOVED=%s\nFULL_DOWN_UP_LOOK=%s\nSTREAM_ERROR=%d\nELAPSED_100HZ_TICKS=%u\n"
        "PLACE_TORCH=%s\nBREAK_TORCH=%s\nVGA_READBACK=%s\nSAVE_RESULT=%d\nLOAD_RESULT=%d\n"
        "AUDIO=SILENT_DEFERRED\nRESULT=%s\n",load_smoke?"LOAD":"NEW",width,columns,mesh,frames,
        input_ok?"PASS":"FAIL",walk_ok?"PASS":"FAIL",look_ok?"PASS":"FAIL",
        !stream_ok,ticks,placed?"PASS":"FAIL",broken?"PASS":"FAIL",vga_ok?"PASS":"FAIL",
        save_result,load_result,ok?"PASS":"FAIL")>=0;
    if(fclose(file)) written=0;
    puts(ok && written ? "Real DOS game smoke: PASS" : "Real DOS game smoke: FAIL");
    exit(!(ok && written));
}
void __wrap_gfx_present(void)
{
    const u8 *frame=g_fb;
    __real_gfx_present();
    if(!smoke) return;
    ++frames;
    if(stage==0) {
        snapshot("C:\\TITLE.RAW",frame);
        start=g_ticks; tap(load_smoke ? 0x26 : 0x39); stage=1;
        return;
    }
    /* During generation the actual game's progress screens also present. */
    if(stage==1 && rendered_frames>0 && !save_busy &&
       g_player.body.h>0 && g_player.health>0 && g_meshQuads>0) {
        snapshot("C:\\WORLD.RAW",frame);
        x0=g_player.body.x; z0=g_player.body.z; key(0x11); /* W held */
        until=g_ticks+100; stage=2;
    } else if(stage==2 && (int)(g_ticks-until)>=0) {
        key(0x91); walk_ok=g_player.body.x!=x0 || g_player.body.z!=z0;
        if(!walk_ok) {
            /* Reload can face the obstruction reached by the first walk.
             * Test real reverse input rather than teleporting/changing terrain. */
            key(0x1f); until=g_ticks+100; stage=12;
        } else {
            key(0xe0); key(0x50); until=g_ticks+1000; stage=3;
        }
    } else if(stage==12 && (int)(g_ticks-until)>=0) {
        key(0x9f); walk_ok=g_player.body.x!=x0 || g_player.body.z!=z0;
        key(0xe0); key(0x50); until=g_ticks+1000; stage=3;
    } else if(stage==3 && (g_player.pitch==-250 || (int)(g_ticks-until)>=0)) {
        key(0xe0); key(0xd0); pitch0=g_player.pitch;
        snapshot("C:\\DOWN.RAW",frame);
        torch_before=torches(); key(0x12); /* E places the original starting torch */
        until=g_ticks+50; stage=10;
    } else if(stage==10 && (int)(g_ticks-until)>=0) {
        key(0x92); torch_after=torches(); placed=torch_after>torch_before;
        snapshot("C:\\PLACE.RAW",frame);
        key(0x24); /* J breaks via real gameplay */ until=g_ticks+100; stage=11;
    } else if(stage==11 && (int)(g_ticks-until)>=0) {
        key(0xa4); broken=placed && torches()<torch_after;
        snapshot("C:\\BREAK.RAW",frame);
        key(0xe0); key(0x48); /* up */ until=g_ticks+1000; stage=4;
    } else if(stage==4 && (g_player.pitch==250 || (int)(g_ticks-until)>=0)) {
        key(0xe0); key(0xc8); look_ok=pitch0==-250 && g_player.pitch==250;
        tap(0x0f); /* Tab inventory */ until=g_ticks+30; stage=5;
    } else if(stage==5 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\INV.RAW",frame);
        tap(0x01); /* existing menu exit */ until=g_ticks+30; stage=6;
    } else if(stage==6 && (int)(g_ticks-until)>=0) {
        tap(0x2e); /* C craft */ until=g_ticks+30; stage=7;
    } else if(stage==7 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\CRAFT.RAW",frame);
        tap(0x01); until=g_ticks+30; stage=8;
    } else if(stage==8 && (int)(g_ticks-until)>=0) {
        stage=9; /* Set before PF9: progress screens reenter this wrapper. */
        if(!load_smoke) tap(0x43); /* F9 -> real game's save_game */
        until=g_ticks+50;
    } else if(stage==9 && !save_busy && (load_smoke || save_result!=-1) &&
              (int)(g_ticks-until)>=0) finish();
    if(!save_busy && g_ticks-start>60000) { input_ok=0; finish(); }
}
