"""DOS main/pause/settings menus, applied after the existing game overlays."""
from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'game.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)

    def one(before, after):
        nonlocal text
        text = replace_once(text, before, after, recipe)

    one('#include "bench.h"\n', '#include "bench.h"\n#include "save_backend.h"\n')
    one('GS_SLEEP,GS_CHEST\n', 'GS_SLEEP,GS_CHEST,GS_MENU\n')
    one('static int state=GS_TITLE;', '''static int state=GS_TITLE;
static int menuPage,menuSel,menuOrigin=GS_TITLE,helpReturn=GS_TITLE;
static int titleSel,saveAvailable;
static const char *menuMessage;
static void draw_title(void);
static void draw_dos_menu(void);
static void menu_open(int page,int origin);
static void menu_key(int k);
static void title_key(int k);''')
    one('static void title_key(int k);',
        'static void title_key(int k);\nstatic const char *menu_save_error(int err);')
    one('titleMsg=save_error_text(err);', 'titleMsg=menu_save_error(err);')
    one('static void save_game(void)', 'static int save_game(void)')
    one('game_message(SAVE_OK==err ? "World saved" : save_error_text(err));\n}',
        '''game_message(SAVE_OK==err ? "World saved" : save_error_text(err));
	if(SAVE_OK==err) saveAvailable=1;
	return err;
}''')
    one('game_message(SAVE_OK==err ? "World saved" : save_error_text(err));',
        'game_message(SAVE_OK==err ? "World saved" : menu_save_error(err));')
    begin = text.index('static void draw_title(void)\n{')
    end = text.index('static void draw_help(void)', begin)
    one(text[begin:end], '#include "menus_draw.inc"\n\n')
    one('"PF6 Music  PF7 Sound  ESC Back",', '"ESC Back   F9 Save in game",')
    one('"F2 Graphics: Low/Med/High/Very Low",', '"ESC Pause   F2 Cycle presets",')
    one('gfx_text_shadow(g_fb,132,206,dos_graphics_message(graphicsPresetSel),C_WHITE,C_BLACK);',
        '/* Graphics values are shown in their settings submenu. */')
    one('/* ---------------- Main loop ---------------- */',
        '#include "menus_input.inc"\n\n/* ---------------- Main loop ---------------- */')
    one('''	case KEY_ESC:
	case KEY_PF1:
		state=GS_HELP;
		break;''', '''	case KEY_ESC:
		menu_open(DM_PAUSE,GS_PLAY);
		break;
	case KEY_PF1:
		helpReturn=GS_PLAY;
		state=GS_HELP;
		break;''')
    one('\tinit_graphics_quality();\n', '\tinit_graphics_quality();\n\tsaveAvailable=dos_save_has_world();\n')
    begin = text.index('\t\t\tcase GS_TITLE:')
    end = text.index('\t\t\tcase GS_PLAY:', begin)
    one(text[begin:end], '''			case GS_TITLE: title_key(k); break;
			case GS_MENU: menu_key(k); break;
''')
    one('state=(0==g_W || 0==g_player.body.h) ? GS_TITLE : GS_PLAY;', 'state=helpReturn;')
    one('''		if(GS_TITLE==state)
		{
			gfx_wait_flip();
			draw_title();
			gfx_present();
			continue;
		}''', '''		if(GS_TITLE==state || (GS_MENU==state && menuOrigin==GS_TITLE) ||
		   (GS_HELP==state && menuOrigin==GS_TITLE && helpReturn!=GS_PLAY))
		{
			gfx_wait_flip();
			draw_title();
			if(GS_TITLE!=state) gfx_clear(g_fb,P(R_SKY,12));
			if(GS_MENU==state) draw_dos_menu();
			if(GS_HELP==state) draw_help();
			gfx_present();
			continue;
		}''')
    one('int inMenu=(GS_INVENTORY==state || GS_CRAFT==state || GS_HELP==state || GS_CHEST==state);',
        'int inMenu=(GS_INVENTORY==state || GS_CRAFT==state || GS_HELP==state || GS_CHEST==state || GS_MENU==state);')
    one('''		case GS_HELP:
			draw_help();''', '''		case GS_MENU:
			draw_dos_menu();
			break;
		case GS_HELP:
			draw_help();''')
    # Load must work from the title even after a previous session existed.
    one('\tstate=GS_PLAY;\n\tgame_message("World loaded");',
        '\tstate=GS_PLAY;\n\tmenuOrigin=GS_PLAY;\n\tgame_message("World loaded");')
    return text.encode('utf-8'), recipe
