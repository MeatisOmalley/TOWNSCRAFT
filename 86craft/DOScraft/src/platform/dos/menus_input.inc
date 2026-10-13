static const char *menu_save_error(int err)
{
	switch(err)
	{
	case SAVE_NO_DISK: return "Save storage unavailable";
	case SAVE_DISK_ERROR: return "Save read/write failed";
	case SAVE_NOT_A_SAVE: return "No saved game";
	case SAVE_WRONG_SIZE: return "Incompatible saved world";
	default: return "Saved world is damaged";
	}
}

static void menu_open(int page,int origin)
{
	menuOrigin=origin;
	menuPage=page; menuSel=0; menuMessage=NULL;
	state=GS_MENU;
}

static void title_key(int k)
{
	if(k==KEY_UP || k==KEY_W || k==KEY_NUM_8) titleSel=(titleSel+3)%4;
	else if(k==KEY_DOWN || k==KEY_S || k==KEY_NUM_2) titleSel=(titleSel+1)%4;
	else if(k==KEY_PF1) { menuOrigin=GS_TITLE; helpReturn=GS_TITLE; state=GS_HELP; }
	else if(k==KEY_L || k==KEY_SPACE || k==KEY_RETURN || k==KEY_NUM_RETURN)
	{
		int choice=k==KEY_L ? 1 : titleSel;
		if(choice==0)
		{
			int err=dos_save_clear_world();
			if(err!=SAVE_OK) { titleMsg=menu_save_error(err); return; }
			saveAvailable=0;
#if defined(TEST_SCENE)
			new_game(12345);
#elif defined(FIXED_SEED)
			new_game(FIXED_SEED);
#else
			new_game(g_ticks*2654435761u+12345);
#endif
			titleMsg=NULL; menuOrigin=GS_PLAY; state=GS_PLAY;
			mineProgress=useRepeat=attackCooldown=0;
		}
		else if(choice==1)
		{
			if(saveAvailable) load_game();
			else titleMsg="No saved game";
		}
		else if(choice==2) menu_open(DM_SETTINGS,GS_TITLE);
		else dos_game_exit();
	}
}

static void menu_back(void)
{
	if(menuPage==DM_PAUSE) state=GS_PLAY;
	else if(menuPage==DM_SETTINGS)
	{
		if(menuOrigin==GS_TITLE) state=GS_TITLE;
		else menu_open(DM_PAUSE,GS_PLAY);
	}
	else menu_open(DM_SETTINGS,menuOrigin);
}

static void menu_graphics_change(int direction)
{
	int textured=g_flatDist ? g_flatDist : g_viewDist;
#ifdef DOS_FAR_FIELD
	if(menuSel==4) { dos_far_change(direction); return; }
#endif
	if(menuSel==0)
	{
		int next=graphicsPresetSel<0 ? DOS_GRAPHICS_DEFAULT :
			(graphicsPresetSel+direction+DOS_GRAPHICS_COUNT)%DOS_GRAPHICS_COUNT;
		apply_graphics_preset(next);
		return;
	}
	if(menuSel==1) { g_renderScale=3-g_renderScale; dos_very_low_mode=0; }
	else if(menuSel==2)
	{
		textured=CLAMP(textured+direction,4,16);
		if(g_viewDist<textured) g_viewDist=textured;
		g_flatDist=textured<g_viewDist ? textured : 0;
	}
	else if(menuSel==3)
	{
		g_viewDist=CLAMP(g_viewDist+direction,textured,16);
		g_flatDist=textured<g_viewDist ? textured : 0;
	}
	else return;
	graphicsPresetSel=-1;
}

static void menu_key(int k)
{
    int graphicsCount=5,graphicsValues=4;
#ifdef DOS_FAR_FIELD
    graphicsCount=6; graphicsValues=5;
#endif
	int count=menuPage==DM_GRAPHICS ? graphicsCount : (menuPage==DM_DIAGNOSTICS || menuPage==DM_CONTROLS ? 2 : 4);
	int activate=k==KEY_RETURN || k==KEY_SPACE || k==KEY_NUM_RETURN;
	if(k==KEY_ESC) { menu_back(); return; }
	if(k==KEY_UP || k==KEY_W || k==KEY_NUM_8) menuSel=(menuSel+count-1)%count;
	else if(k==KEY_DOWN || k==KEY_S || k==KEY_NUM_2) menuSel=(menuSel+1)%count;
	else if(menuPage==DM_GRAPHICS && menuSel<graphicsValues &&
	        (activate || k==KEY_LEFT || k==KEY_RIGHT || k==KEY_A || k==KEY_D))
		menu_graphics_change(k==KEY_LEFT || k==KEY_A ? -1 : 1);
	else if(activate)
	{
		if(menuPage==DM_PAUSE)
		{
			if(menuSel==0) state=GS_PLAY;
			else if(menuSel==1) menu_open(DM_SETTINGS,GS_PLAY);
			else
			{
				int leave=menuSel==3;
				int err=save_game();
				menuMessage=err==SAVE_OK ? "Game saved" : menu_save_error(err);
				if(leave && err==SAVE_OK)
				{
					state=GS_TITLE; titleSel=1; menuOrigin=GS_TITLE;
					titleMsg="Game saved";
				}
			}
		}
		else if(menuPage==DM_SETTINGS)
		{
			if(menuSel==3) menu_back();
			else menu_open(menuSel==0 ? DM_GRAPHICS : (menuSel==1 ? DM_CONTROLS : DM_DIAGNOSTICS),menuOrigin);
		}
		else if(menuPage==DM_DIAGNOSTICS && menuSel==0) showDebug=!showDebug;
		else if(menuPage==DM_CONTROLS && menuSel==0) { helpReturn=GS_MENU; state=GS_HELP; }
		else menu_back();
	}
}
