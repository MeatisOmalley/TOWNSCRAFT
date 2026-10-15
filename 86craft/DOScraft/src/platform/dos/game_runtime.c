/* Link-only presentation wrapper. Normal play goes straight to the unchanged
 * platform adapter. /SMOKE drives only real AT input and records diagnostics;
 * no source gameplay/render rewrite, test-world substitution or reduced budget. */
#include "sys.h"
#include "world.h"
#include "player.h"
#include "inventory.h"
#include "mobs.h"
#include "render.h"
#include "gfx.h"
#include "game.h"
#include "system.h"
#include "save_backend.h"
#include "hdd_backend.h"
#include "video_backend.h"
#ifdef DOS_GAME_AUDIO
#include "game_audio.h"
#include "sb_pcm.h"
#include "audio_voices.h"
#endif
#ifdef DOS_FAR_FIELD
#include "far_field.h"
extern u32 dos_far_samples,dos_far_strips,dos_far_pan_builds,dos_far_frames;
static int far_option_ok=1;
#endif
#ifdef DOS_GRAPHICS_PRESETS
#include "graphics_presets.h"
#endif
#include "save.h"
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int smoke,load_smoke,stage,frames,input_ok=1;
static unsigned int until,start;
static int x0,z0,pitch0,walk_ok,look_ok;
static int save_busy,save_result=-1,load_result=-1;
static int post_load_render;
static int placed,broken,torch_before,torch_after,vga_ok=1;
static const char *vga_failed_snapshot;
static int despawn_ok;
static int chicken_ok=1;
static int chicken_loaded(void)
{
    int found=0;
    for(int i=0;i<MAX_MOBS;++i) found|=g_mobs[i].type==MOB_CHICKEN;
    return found && inv_count(I_RAW_CHICKEN)>0 && inv_count(I_FEATHER)>0;
}
#ifdef DOS_LARGE_WORLD
extern int world_verify_large_travel(int x,int z);
static int terrain_persist_ok=1;
static int edge_stream_ok=1;
#ifdef DOS_STREAM_LIGHT_CACHE
extern int world_verify_stream_light(void);
extern u32 g_streamLightStats[4];
static int stream_light_ok=1;
#endif
#endif
static int rendered_frames;
#ifdef DOS_VIEWMODEL
static int held_done,held_selection;
static Slot held_slot;
#endif
static int fullres_ok;
static unsigned int inventory_ticks,inventory_started_at;
static int inventory_frames,inventory_started_frame;
#ifdef DOS_GRAPHICS_PRESETS
static int presets_ok;
static int menus_ok=1,pause_x,pause_z,pause_time;
static int preset_matches(int scale,int view,int flat)
{
    return g_renderScale==scale && g_viewDist==view && g_flatDist==flat &&
        !g_distanceMode && g_textureSubdivision==SUBDIV_ADAPTIVE && !g_occlusion;
}
#endif
void dos_game_exit(void)
{
    dos_save_shutdown(); dos_hdd_shutdown();
    dos_system_shutdown(); dos_video_restore();
    exit(0);
}
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
#ifdef DOS_RASTER_FINISH_SKIP
extern void raster_complete_odd_rows(void);
#endif
static void snapshot(const char *path,const u8 *frame)
{
    unsigned char palette[768];
    int i,row,ok=1;
#ifdef DOS_LOW_PLANAR
    /* Planar view rows live in presenter scratch that verification reuses. */
    if(dos_video_last_present_proven()) raster_complete_odd_rows();
#endif
    if(dos_video_verify(frame,512)) {
        vga_ok=0;
        if(!vga_failed_snapshot) vga_failed_snapshot=path;
    }
#ifdef DOS_RASTER_FINISH_SKIP
    /* Capture the displayed image after verifying the optimized source. */
    if(dos_video_last_present_proven()) raster_complete_odd_rows();
#endif
#ifdef DOS_VERY_LOW
    dos_video_complete_very_low_source((u8 *)frame,512);
#ifdef DOS_VERY_LOW_TEXELS
    if(dos_potato_mode && dos_video_last_present_proven()) {
        extern u8 dos_very_low_rows[VIEW_H/2];
        for(int y=1;y<VIEW_H/2;y+=2)
            /* UI drawn after the world can change the crosshair, mining,
             * message, item-name and debug bands independently. */
            if(y*2>=114 && y*2<176 && (dos_very_low_rows[y]&2) &&
               memcmp(frame+y*1024,frame+(y-1)*1024,320))
                vga_ok=0;
    }
#endif
#endif
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
static void arrow(unsigned char byte) { key(0xe0); key(byte); key(0xe0); key(byte|128); }
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
#ifdef DOS_GAME_AUDIO
    const char *audio_backend=dos_audio_backend();
    unsigned int audio_events=dos_audio_events,audio_blocks=dos_sb_completions,audio_faults=dos_sb_stream_faults;
    unsigned int audio_frames=dos_audio_mixed_frames;
    int audio_ok=!strcmp(audio_backend,"SB_SAMPLED") && audio_events && audio_blocks && !audio_faults;
#endif
    /* A completed diagnostic exits cleanly, never kill a live save session. */
    dos_save_shutdown(); dos_hdd_shutdown();
    dos_system_shutdown(); dos_video_restore();
    FILE *file=fopen(load_smoke ? "C:\\PLAYLOAD.TXT" : "C:\\PLAYNEW.TXT","wb");
    int storage_ok=load_smoke ? load_result==SAVE_OK : save_result==SAVE_OK;
#ifdef DOS_LARGE_WORLD
    const int expected_width=1024;
#else
    const int expected_width=256;
#endif
    int ok=input_ok && walk_ok && look_ok && stream_ok && width==expected_width && mesh>0 &&
        storage_ok && placed && broken && vga_ok && fullres_ok && despawn_ok && chicken_ok;
#ifdef DOS_LARGE_WORLD
    ok=ok && terrain_persist_ok && edge_stream_ok;
#ifdef DOS_STREAM_LIGHT_CACHE
    ok=ok && stream_light_ok;
#endif
#endif
#ifdef DOS_GRAPHICS_PRESETS
    ok=ok && presets_ok && menus_ok;
#endif
#ifdef DOS_FAR_FIELD
    ok=ok && far_option_ok && dos_far_frames>0 && dos_far_pan_builds>=256;
#endif
#ifdef DOS_GAME_AUDIO
    ok=ok && audio_ok;
#endif
    if(!file) exit(1);
    int written=fprintf(file,"DOScraft real game-loop smoke\nMODE=%s\nWORLD_WIDTH=%d\n"
        "RESIDENT_COLUMNS=%d\nMESH_QUADS=%u\nPRESENTED_FRAMES=%d\nAT_INPUT=%s\n"
        "WALK_MOVED=%s\nFULL_DOWN_UP_LOOK=%s\nSTREAM_ERROR=%d\nELAPSED_100HZ_TICKS=%u\n"
        "PLACE_TORCH=%s\nBREAK_TORCH=%s\nVGA_READBACK=%s\nFULL_RES_OPTION=%s\nMOB_DESPAWN=%s\nSAVE_RESULT=%d\nLOAD_RESULT=%d\n"
        "INVENTORY_PRESENTS=%d\nINVENTORY_100HZ_TICKS=%u\n"
        "RESULT=%s\n",load_smoke?"LOAD":"NEW",width,columns,mesh,frames,
        input_ok?"PASS":"FAIL",walk_ok?"PASS":"FAIL",look_ok?"PASS":"FAIL",
        !stream_ok,ticks,placed?"PASS":"FAIL",broken?"PASS":"FAIL",vga_ok?"PASS":"FAIL",
        fullres_ok?"PASS":"FAIL",despawn_ok?"PASS":"FAIL",save_result,load_result,inventory_frames,inventory_ticks,
        ok?"PASS":"FAIL")>=0;
#ifdef DOS_GAME_AUDIO
    if(fprintf(file,"AUDIO=%s\nAUDIO_EVENTS=%u\nAUDIO_BLOCKS=%u\nAUDIO_FAULTS=%u\nAUDIO_MIXED_FRAMES=%u\n",
               audio_backend,audio_events,audio_blocks,audio_faults,audio_frames)<0)written=0;
#else
    if(fprintf(file,"AUDIO=SILENT_DEFERRED\n")<0)written=0;
#endif
#ifdef DOS_GRAPHICS_PRESETS
    if(fprintf(file,"GRAPHICS_PRESETS=%s\n",presets_ok?"PASS":"FAIL")<0) written=0;
    if(fprintf(file,"MENUS=%s\n",menus_ok?"PASS":"FAIL")<0) written=0;
#endif
#ifdef DOS_LARGE_WORLD
    if(fprintf(file,"TERRAIN_PERSIST=%s\nEDGE_STREAM=%s\n",
               terrain_persist_ok?"PASS":"FAIL",edge_stream_ok?"PASS":"FAIL")<0) written=0;
#endif
    if(vga_failed_snapshot && fprintf(file,"VGA_FAILED_SNAPSHOT=%s\n",vga_failed_snapshot)<0) written=0;
    if(fprintf(file,"CHICKENS=%s\n",chicken_ok?"PASS":"FAIL")<0) written=0;
#ifdef DOS_FAR_FIELD
    if(fprintf(file,"DISTANT_TERRAIN=%s\nFAR_DRAWS=%u\nPANORAMA_COLUMNS=%u\n",
        far_option_ok?"PASS":"FAIL",dos_far_frames,dos_far_pan_builds)<0) written=0;
#endif
#ifdef DOS_STREAM_LIGHT_CACHE
    if(fprintf(file,"LIGHT_CACHE=%s\nLIGHT_CACHE_STATS=%u,%u,%u,%u\n",stream_light_ok?"PASS":"FAIL",
        g_streamLightStats[0],g_streamLightStats[1],g_streamLightStats[2],g_streamLightStats[3])<0) written=0;
#endif
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
#ifdef DOS_GRAPHICS_PRESETS
        start=g_ticks; arrow(0x50); arrow(0x50); tap(0x1c);
        until=g_ticks+30; stage=30;
#else
        start=g_ticks; tap(load_smoke ? 0x26 : 0x39); stage=1;
#endif
        return;
    }
#ifdef DOS_GRAPHICS_PRESETS
    if(stage==30 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && rendered_frames==0;
        snapshot("C:\\TSET.RAW",frame); tap(0x1c); until=g_ticks+30; stage=31;
    } else if(stage==31 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && rendered_frames==0;
        snapshot("C:\\TGFX.RAW",frame);
#ifdef DOS_FAR_FIELD
        far_option_ok=far_option_ok && !dos_far_enabled && dos_far_available();
        arrow(0x50); arrow(0x50); arrow(0x50); arrow(0x50); tap(0x1c);
        until=g_ticks+30; stage=50; return;
    } else if(stage==50 && (int)(g_ticks-until)>=0) {
        far_option_ok=far_option_ok && dos_far_enabled==DOS_FAR_VERY_LOW && preset_matches(2,8,0);
        snapshot("C:\\FAROPT.RAW",frame);
        arrow(0x4d); until=g_ticks+30; stage=51; return;
    } else if(stage==51 && (int)(g_ticks-until)>=0) {
        far_option_ok=far_option_ok && dos_far_enabled==DOS_FAR_LOW;
        snapshot("C:\\FARLOW.RAW",frame);
        arrow(0x4d); until=g_ticks+30; stage=52; return;
    } else if(stage==52 && (int)(g_ticks-until)>=0) {
        far_option_ok=far_option_ok && dos_far_enabled==DOS_FAR_MEDIUM;
        arrow(0x4d); until=g_ticks+30; stage=53; return;
    } else if(stage==53 && (int)(g_ticks-until)>=0) {
        far_option_ok=far_option_ok && dos_far_enabled==DOS_FAR_HIGH;
        snapshot("C:\\FARHIGH.RAW",frame);
#endif
        tap(0x01); tap(0x01);
        /* Title cursor still points at Settings; move back to New Game. */
        arrow(0x48); arrow(0x48); tap(load_smoke ? 0x26 : 0x1c); stage=1;
        return;
    }
#endif
    /* During generation the actual game's progress screens also present. */
    if(stage==1 && rendered_frames>0 && !save_busy &&
       g_player.body.h>0 && g_player.health>0 && g_meshQuads>0) {
#ifdef DOS_VIEWMODEL
        if(!held_done) {
            held_selection=g_player.selected; held_slot=g_inv[held_selection];
            g_inv[held_selection].item=g_inv[held_selection].count=0;
            until=g_ticks+30;stage=60;return;
        }
#endif
        if(load_smoke) chicken_ok=chicken_loaded();
        snapshot("C:\\WORLD.RAW",frame);
#ifdef DOS_LARGE_WORLD
        if(load_smoke) {
            int x=g_spawnX+3,z=g_spawnZ+3,y=WH-1;
            terrain_persist_ok=world_column_ready(x/CS,z/CS) && wget(x,y,z)==B_COBBLE;
        }
#endif
        {
            int px=g_player.body.x/FU, x=px+40<g_W ? px+40 : px-40;
            int pig=mob_spawn(MOB_PIG,x*FU,g_player.body.y,g_player.body.z);
            int zombie=mob_spawn(MOB_ZOMBIE,x*FU,g_player.body.y,g_player.body.z);
            int chicken=mob_spawn(MOB_CHICKEN,x*FU,g_player.body.y,g_player.body.z);
            mobs_regions_tick();
            despawn_ok=pig>=0 && zombie>=0 && g_mobs[pig].type==MOB_NONE &&
                g_mobs[zombie].type==MOB_NONE && mobs_dormant_count()==0;
            chicken_ok=chicken_ok && chicken>=0 && g_mobs[chicken].type==MOB_NONE;
        }
        x0=g_player.body.x; z0=g_player.body.z; key(0x11); /* W held */
        until=g_ticks+100; stage=2;
#ifdef DOS_VIEWMODEL
    } else if(stage>=60 && stage<=63 && (int)(g_ticks-until)>=0) {
        const char *name=stage==60 ? "C:\\HAND.RAW" : stage==61 ? "C:\\HELD.RAW" :
            stage==62 ? "C:\\TOOL.RAW" : "C:\\TORCH.RAW";
        snapshot(name,frame);
        if(stage==63) {
            g_inv[held_selection]=held_slot;g_player.selected=held_selection;
            held_done=1;stage=1;return;
        }
        g_inv[held_selection].item=stage==60 ? B_COBBLE : stage==61 ? I_IRON_PICK : B_TORCH;
        g_inv[held_selection].count=1;until=g_ticks+30;++stage;
#endif
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
        tap(0x0f); /* Tab inventory */
        inventory_started_at=g_ticks; inventory_started_frame=frames;
        until=g_ticks+30; stage=5;
    } else if(stage==5 && (int)(g_ticks-until)>=0) {
        inventory_ticks=g_ticks-inventory_started_at;
        inventory_frames=frames-inventory_started_frame;
        snapshot("C:\\INV.RAW",frame);
        tap(0x01); /* existing menu exit */ until=g_ticks+30; stage=6;
    } else if(stage==6 && (int)(g_ticks-until)>=0) {
        tap(0x2e); /* C craft */ until=g_ticks+30; stage=7;
    } else if(stage==7 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\CRAFT.RAW",frame);
        tap(0x01); until=g_ticks+30; stage=13;
    } else if(stage==13 && (int)(g_ticks-until)>=0) {
        tap(0x3c); /* F2: first graphics option step. */
        until=g_ticks+30; stage=14;
    } else if(stage==14 && (int)(g_ticks-until)>=0) {
#ifdef DOS_GRAPHICS_PRESETS
        presets_ok=preset_matches(2,8,0);
        snapshot("C:\\MEDIUM.RAW",frame);
        tap(0x3c); until=g_ticks+30; stage=15;
    } else if(stage==15 && (int)(g_ticks-until)>=0) {
        presets_ok=presets_ok && preset_matches(1,8,0);
#endif
        fullres_ok=g_renderScale==1;
        snapshot("C:\\FULL.RAW",frame);
        tap(0x3c); until=g_ticks+30; stage=8;
    } else if(stage==8 && (int)(g_ticks-until)>=0) {
#ifdef DOS_GRAPHICS_PRESETS
        presets_ok=presets_ok && preset_matches(2,8,0) && dos_very_low_mode;
        snapshot("C:\\VERYLOW.RAW",frame);
        tap(0x3c); until=g_ticks+30; stage=16;
    } else if(stage==16 && (int)(g_ticks-until)>=0) {
        presets_ok=presets_ok && preset_matches(2,8,0) && !dos_very_low_mode;
        snapshot("C:\\LOW.RAW",frame);
        tap(0x01); until=g_ticks+30; stage=17;
    } else if(stage==17 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\PAUSE.RAW",frame);
        pause_x=g_player.body.x; pause_z=g_player.body.z; pause_time=g_time;
        key(0x11); until=g_ticks+50; stage=18;
    } else if(stage==18 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && pause_x==g_player.body.x && pause_z==g_player.body.z && pause_time==g_time;
        /* W also navigates up in the pause menu: row 0 wrapped to row 3. */
        key(0x91); arrow(0x50); arrow(0x50); tap(0x1c); until=g_ticks+30; stage=19;
    } else if(stage==19 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\SETTINGS.RAW",frame); tap(0x1c); until=g_ticks+30; stage=20;
    } else if(stage==20 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\GRAPHICS.RAW",frame);
        arrow(0x50); arrow(0x4d); until=g_ticks+30; stage=55;
    } else if(stage==55 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && g_renderScale==2 && dos_very_low_mode && !dos_potato_mode;
        tap(0x01); tap(0x01); tap(0x01); until=g_ticks+30; stage=56;
    } else if(stage==56 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\MENU80.RAW",frame);
        tap(0x01); arrow(0x50); tap(0x1c); tap(0x1c);
        arrow(0x50); arrow(0x4d); until=g_ticks+30; stage=57;
    } else if(stage==57 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && g_renderScale==2 && dos_very_low_mode && dos_potato_mode;
        snapshot("C:\\POTMENU.RAW",frame);
        tap(0x01); tap(0x01); tap(0x01); until=g_ticks+30; stage=58;
    } else if(stage==58 && (int)(g_ticks-until)>=0) {
        snapshot("C:\\POTATO.RAW",frame);
        tap(0x01); arrow(0x50); tap(0x1c); tap(0x1c);
        arrow(0x50); arrow(0x4d); arrow(0x4d); until=g_ticks+30; stage=59;
    } else if(stage==59 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && g_renderScale==2 && !dos_very_low_mode && !dos_potato_mode;
        arrow(0x50); arrow(0x4d); arrow(0x50); arrow(0x4d);
        until=g_ticks+30; stage=21;
    } else if(stage==21 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && g_viewDist==12 && g_flatDist==10;
        snapshot("C:\\CUSTOM.RAW",frame);
        arrow(0x48); arrow(0x48); arrow(0x48); tap(0x1c);
        tap(0x01); tap(0x01); tap(0x01); until=g_ticks+30; stage=24;
    } else if(stage==24 && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && preset_matches(2,8,0);
#endif
        stage=9; /* Set before PF9: progress screens reenter this wrapper. */
#ifdef DOS_LARGE_WORLD
        {
            int x=g_spawnX+3,z=g_spawnZ+3,y=WH-1;
            if(!load_smoke) {
            terrain_persist_ok=world_column_ready(x/CS,z/CS) && wget(x,y,z)!=B_COBBLE;
            if(terrain_persist_ok) {
                world_set(x,y,z,B_COBBLE);
                terrain_persist_ok=wget(x,y,z)==B_COBBLE;
            }
            }
            edge_stream_ok=world_verify_large_travel(x,z);
#ifdef DOS_STREAM_LIGHT_CACHE
            stream_light_ok=world_verify_stream_light();
#endif
            terrain_persist_ok=terrain_persist_ok && world_column_ready(x/CS,z/CS) &&
                wget(x,y,z)==B_COBBLE;
        }
#endif
        if(!load_smoke) {
            int chicken=mob_spawn(MOB_CHICKEN,g_player.body.x+FU/2,
                g_player.body.y,g_player.body.z+2*FU);
            chicken_ok=chicken_ok && chicken>=0;
            if(chicken>=0) { g_mobs[chicken].aiTimer=500; g_mobs[chicken].walking=0; }
            chicken_ok=chicken_ok && inv_add(I_RAW_CHICKEN,1)==0 && inv_add(I_FEATHER,2)==0;
#ifdef DOS_GRAPHICS_PRESETS
            tap(0x01); arrow(0x50); arrow(0x50); arrow(0x50); tap(0x1c);
#else
            tap(0x43); /* F9 -> real game's save_game */
#endif
        }
        until=g_ticks+50;
    } else if(stage==9 && !save_busy && (load_smoke || save_result!=-1) &&
              (int)(g_ticks-until)>=0) {
#ifdef DOS_GRAPHICS_PRESETS
        if(!load_smoke && save_result==SAVE_OK) {
            snapshot("C:\\SAVED.RAW",frame);
            post_load_render=rendered_frames;
            tap(0x26); until=g_ticks+30; stage=40;
        } else finish();
    } else if(stage==40 && !save_busy && load_result!=-1 &&
              rendered_frames>post_load_render && (int)(g_ticks-until)>=0) {
        menus_ok=menus_ok && load_result==SAVE_OK;
        chicken_ok=chicken_ok && chicken_loaded();
        snapshot("C:\\CHICKEN.RAW",frame);
        finish();
#else
        finish();
#endif
    }
    if(!save_busy && g_ticks-start>60000) { input_ok=0; finish(); }
}

/* An idle inventory intentionally has no presents. Advance both menu smoke
 * steps from its input loop without requiring host mouse movement. */
void dos_game_inventory_idle(void)
{
    if(!smoke || (int)(g_ticks-until)<0) return;
    if(stage==5) {
        inventory_ticks=g_ticks-inventory_started_at;
        inventory_frames=frames-inventory_started_frame;
        snapshot("C:\\INV.RAW",g_fb);
        tap(0x01); /* existing menu exit */
        until=g_ticks+30;
        stage=6;
    } else if(stage==7) {
        snapshot("C:\\CRAFT.RAW",g_fb);
        tap(0x01);
        until=g_ticks+30;
        stage=13;
    }
}
