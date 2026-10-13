/* Included in staged game.c; the pinned Towns sources remain immutable. */
enum { DM_PAUSE,DM_SETTINGS,DM_GRAPHICS,DM_DIAGNOSTICS,DM_CONTROLS };

static void menu_row(int row,const char *label,int selected,int enabled)
{
	int y=66+row*22;
	if(selected) gfx_rect(g_fb,32,y-5,256,20,P(R_GRAY,7));
	gfx_text_center(g_fb,y,label,enabled ? (selected ? C_YELLOW : C_WHITE) : P(R_GRAY,9),C_BLACK);
}

static void draw_title(void)
{
	static const char *const rows[]={"New Game","Load Game","Settings","Quit to DOS"};
	int i;
	gfx_clear(g_fb,P(R_SKY,12));
	gfx_rect(g_fb,0,180,SCR_W,60,P(R_DIRT,9));
	gfx_rect(g_fb,0,176,SCR_W,6,P(R_GRASS,11));
	ui_text_scaled(g_fb,64+2,20+2,"DOSCRAFT",P(R_GRAY,3),3);
	ui_text_scaled(g_fb,64,20,"DOSCRAFT",C_WHITE,3);
	ui_panel(g_fb,24,54,272,112);
	for(i=0;i<ARRAY_LEN(rows);++i) menu_row(i,rows[i],titleSel==i,i!=1 || saveAvailable);
	gfx_text_center(g_fb,184,"New Game replaces the single save.",C_WHITE,C_BLACK);
	gfx_text_center(g_fb,204,saveAvailable ? "One save slot: occupied" : "One save slot: empty",C_WHITE,C_BLACK);
	gfx_text_center(g_fb,224,"Up/Down select   Enter open",C_WHITE,C_BLACK);
	if(titleMsg) gfx_text_center(g_fb,170,titleMsg,C_YELLOW,C_BLACK);
}

static void menu_value(int row,const char *label,const char *value)
{
	char text[40];
	int n=strlen(label);
	memcpy(text,label,n);
	memcpy(text+n,value,strlen(value)+1);
	menu_row(row,text,menuSel==row,1);
}

static void draw_dos_menu(void)
{
	static const char *const pause[]={"Resume Game","Settings","Save Game","Save & Main Menu"};
	static const char *const settings[]={"Graphics >","Controls >","Diagnostics >","Back"};
	static const char *const preset[]={"Low","Medium","High","Very Low"};
	static const char *const headings[]={"GAME PAUSED","SETTINGS","GRAPHICS","DIAGNOSTICS","CONTROLS"};
	int i;
	char number[16];
	/* Fixed bounds erase previous submenu contents on every display page. */
	ui_panel(g_fb,20,16,280,208);
	gfx_text_center(g_fb,30,headings[menuPage],C_YELLOW,C_BLACK);
	if(menuPage==DM_PAUSE || menuPage==DM_SETTINGS)
	{
		const char *const *rows=menuPage==DM_PAUSE ? pause : settings;
		for(i=0;i<4;++i) menu_row(i,rows[i],menuSel==i,1);
	}
	else if(menuPage==DM_GRAPHICS)
	{
		menu_value(0,"Preset: ",graphicsPresetSel<0 ? "Custom" : preset[graphicsPresetSel]);
		menu_value(1,"View resolution: ",g_renderScale==1 ? "320x200" : (dos_very_low_mode ? "80x100" : "160x100"));
		menu_value(2,"Textured distance: ",itoa_dec(g_flatDist ? g_flatDist : g_viewDist,number));
		menu_value(3,"Flat distance: ",itoa_dec(g_viewDist,number));
		menu_row(4,"Back",menuSel==4,1);
		gfx_text_center(g_fb,180,"Textured near; flat out to limit.",P(R_GRAY,11),C_BLACK);
		gfx_text_center(g_fb,192,"Distances in blocks (4-16).",P(R_GRAY,11),C_BLACK);
	}
	else if(menuPage==DM_DIAGNOSTICS)
	{
		menu_value(0,"Debug overlay: ",showDebug ? "On" : "Off");
		menu_row(1,"Back",menuSel==1,1);
	}
	else
	{
		menu_row(0,"View Controls",menuSel==0,1);
		menu_row(1,"Back",menuSel==1,1);
	}
	if(menuMessage) gfx_text_center(g_fb,202,menuMessage,C_YELLOW,C_BLACK);
	gfx_text_center(g_fb,212,menuPage==DM_GRAPHICS ? "Arrows change   Enter   ESC Back" : "Up/Down   Enter   ESC Back",C_WHITE,C_BLACK);
}
