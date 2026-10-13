#include <assert.h>
#include <stdio.h>
#include "sys.h"
#include "save.h"
#include "graphics_presets.h"
#include "far_field.h"
int dos_far_enabled;
int dos_far_available(void) { return 1; }
void dos_far_change(int direction) { dos_far_enabled=(dos_far_enabled+direction+4)%4; }
enum { GS_TITLE,GS_PLAY,GS_HELP,GS_MENU };
enum { DM_PAUSE,DM_SETTINGS,DM_GRAPHICS,DM_DIAGNOSTICS,DM_CONTROLS };
static int state,menuPage,menuSel,menuOrigin,helpReturn,titleSel,saveAvailable;
static int mineProgress,useRepeat,attackCooldown,showDebug,graphicsPresetSel;
static int g_viewDist,g_flatDist,g_renderScale;
static const char *menuMessage,*titleMsg;
volatile u32 g_ticks;
static int clearResult=SAVE_OK,saveResult=SAVE_OK,clearCalls,newCalls,loadCalls,saveCalls,quitCalls;
int dos_save_clear_world(void) { ++clearCalls; return clearResult; }
void dos_game_exit(void) { ++quitCalls; }
const char *save_error_text(int err) { (void)err; return "Disk error"; }
static void new_game(u32 seed) { (void)seed; ++newCalls; }
static void load_game(void) { ++loadCalls; state=GS_PLAY; }
static int save_game(void) { ++saveCalls; if(saveResult==SAVE_OK) saveAvailable=1; return saveResult; }
static void apply_graphics_preset(int index) {
    const DosGraphicsSettings *s=dos_graphics_settings(index);
    graphicsPresetSel=index; g_viewDist=s->view_distance;
    g_flatDist=s->flat_distance; g_renderScale=s->render_scale;
    dos_very_low_mode=index==DOS_GRAPHICS_VERY_LOW;
}
#include "menus_input.inc"
int main(void) {
    state=GS_TITLE;
    title_key(KEY_L); assert(!loadCalls && titleMsg);
    clearResult=SAVE_DISK_ERROR; title_key(KEY_RETURN);
    assert(clearCalls==1 && !newCalls && state==GS_TITLE);
    clearResult=SAVE_OK; saveAvailable=1; title_key(KEY_RETURN);
    assert(clearCalls==2 && newCalls==1 && state==GS_PLAY && !saveAvailable);
    menu_open(DM_PAUSE,GS_PLAY); assert(state==GS_MENU);
    menu_key(KEY_DOWN); menu_key(KEY_RETURN);
    assert(menuPage==DM_SETTINGS && menuOrigin==GS_PLAY);
    menu_key(KEY_RETURN); assert(menuPage==DM_GRAPHICS);
    apply_graphics_preset(DOS_GRAPHICS_VERY_LOW); menuSel=1; menu_key(KEY_RETURN);
    assert(g_renderScale==1 && !dos_very_low_mode && graphicsPresetSel==-1);
    apply_graphics_preset(DOS_GRAPHICS_MEDIUM);
    menuSel=2; menu_key(KEY_RIGHT); assert(!g_flatDist && g_viewDist==9 && graphicsPresetSel==-1);
    menu_key(KEY_RIGHT); assert(!g_flatDist && g_viewDist==10);
    menu_key(KEY_RIGHT); assert(!g_flatDist && g_viewDist==11);
    menuSel=3; for(int i=0;i<40;++i) menu_key(KEY_RIGHT);
    assert(g_viewDist==16 && g_flatDist==11);
    for(int i=0;i<40;++i) menu_key(KEY_LEFT);
    assert(g_viewDist==11 && !g_flatDist);
    menuSel=2; for(int i=0;i<40;++i) menu_key(KEY_LEFT);
    assert(g_flatDist==4 && g_viewDist==11);
    menuSel=0; menu_key(KEY_RETURN); assert(graphicsPresetSel==DOS_GRAPHICS_LOW && g_viewDist==8 && !g_flatDist);
#ifdef DOS_FAR_FIELD
    menuSel=4; menu_key(KEY_RETURN); assert(dos_far_enabled && g_viewDist==8 && graphicsPresetSel==DOS_GRAPHICS_LOW);
    menu_key(KEY_LEFT); assert(!dos_far_enabled);
    menu_key(KEY_LEFT); assert(dos_far_enabled==3);
    menuSel=0; menu_key(KEY_RETURN); assert(dos_far_enabled==3 && g_viewDist==8);
    menuSel=4; menu_key(KEY_RIGHT); assert(!dos_far_enabled);
#endif
    menu_key(KEY_ESC); assert(menuPage==DM_SETTINGS);
    menuSel=1; menu_key(KEY_RETURN); menu_key(KEY_RETURN);
    assert(state==GS_HELP && helpReturn==GS_MENU && menuOrigin==GS_PLAY);
    state=helpReturn; menu_key(KEY_ESC); menu_key(KEY_ESC);
    assert(menuPage==DM_PAUSE); menu_key(KEY_ESC); assert(state==GS_PLAY);
    menu_open(DM_PAUSE,GS_PLAY); menuSel=3; saveResult=SAVE_DISK_ERROR;
    menu_key(KEY_RETURN); assert(saveCalls==1 && state==GS_MENU && menuMessage);
    saveResult=SAVE_OK; menu_key(KEY_RETURN);
    assert(saveCalls==2 && state==GS_TITLE && saveAvailable && titleSel==1);
    title_key(KEY_RETURN); assert(loadCalls==1 && state==GS_PLAY);
    state=GS_TITLE; titleSel=2; title_key(KEY_RETURN);
    assert(state==GS_MENU && menuOrigin==GS_TITLE);
    menu_key(KEY_RETURN); menu_key(KEY_ESC); menu_key(KEY_ESC); assert(state==GS_TITLE);
    titleSel=3; title_key(KEY_RETURN); assert(quitCalls==1);
    puts("PASS: menu navigation, distance bounds, single slot and save failures");
    return 0;
}
