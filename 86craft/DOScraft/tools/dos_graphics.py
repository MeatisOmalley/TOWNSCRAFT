"""Bounded game.c overlay for the authored DOS Low/Medium/High policy.

Integration: pass staged game.c bytes through adapt('game.c', data), record
the returned exact replacement recipe, compile graphics_presets.c with the
staged imported headers, and add src/platform/dos to game.c's include path.
This module never writes the immutable import or changes any other source.
"""


REPLACEMENTS = (
    ('#include "bench.h"\n',
     '#include "bench.h"\n#include "graphics_presets.h"\n'),
    ('''/* View distance presets (PF3): textured distance, total distance (faces in
   between are drawn flat), offscreen subdivision culling. */
static const u8 viewPreset[][3]={{8,8,0},{8,10,0},{10,12,0},{6,12,1},{6,16,1}};
static int viewPresetSel;

static void apply_view_preset(int i)
{
	viewPresetSel=i;
	g_viewDist=viewPreset[i][1];
	g_flatDist=(viewPreset[i][0]<viewPreset[i][1]) ? viewPreset[i][0] : 0;
	g_distanceMode=viewPreset[i][2];
}
''', '''/* DOS graphics policy: explicit Low default, changed only by F2. */
static int graphicsPresetSel=DOS_GRAPHICS_DEFAULT;

static void apply_graphics_preset(int i)
{
	const DosGraphicsSettings *settings=dos_graphics_settings(i);
	graphicsPresetSel=dos_graphics_index(i);
	dos_very_low_mode=graphicsPresetSel==DOS_GRAPHICS_VERY_LOW;
	g_renderScale=settings->render_scale;
	g_viewDist=settings->view_distance;
	g_flatDist=settings->flat_distance;
	g_distanceMode=settings->distance_mode;
	g_textureSubdivision=settings->texture_subdivision;
	g_occlusion=0;
}
'''),
    ('static void auto_detect_quality(void)\n',
     'static void init_graphics_quality(void)\n'),
    ('''	/* Measured in Tsugaru: 386 16MHz ~120000, default profile ~290000 */
	if(n<170000)
	{
		g_renderScale=2;
		apply_view_preset(0);
	}
	else if(n<450000)
	{
		g_renderScale=2;
		apply_view_preset(2);
	}
	else
	{
		g_renderScale=1;
		apply_view_preset(2);
	}
''', '''	/* CPU loop remains diagnostic; quality always starts at Low. */
	apply_graphics_preset(DOS_GRAPHICS_DEFAULT);
'''),
    ('\tauto_detect_quality();\n', '\tinit_graphics_quality();\n'),
    ('''	case KEY_PF2:
		g_renderScale=3-g_renderScale;
		game_message(1==g_renderScale ? "Resolution: 320x200" : "Resolution: 160x100");
		break;
''', '''	case KEY_PF2:
		apply_graphics_preset(dos_graphics_next(graphicsPresetSel));
		game_message(dos_graphics_message(graphicsPresetSel));
		break;
'''),
    ('''	case KEY_PF3:
		{
			static const char *const names[]=
			{
				"View: 8 blocks",
				"View: 8 textured, 10 flat",
				"View: 10 textured, 12 flat",
				"Distance: 6 textured, 12 total",
				"Distance: 6 textured, 16 total",
			};
			apply_view_preset((viewPresetSel+1)%ARRAY_LEN(viewPreset));
			game_message(names[viewPresetSel]);
		}
		break;
''', ''),
    ('''	case KEY_PF8:
		g_occlusion=!g_occlusion;
		game_message(g_occlusion ? "Occlusion culling: on" : "Occlusion culling: off");
		break;
	case KEY_PF10:
		{
			static const char *const names[SUBDIV_COUNT]={
				"Near textures: adaptive",
				"Near textures: full subdivision",
				"Near textures: fast / no split"
			};
			g_textureSubdivision=(g_textureSubdivision+1)%SUBDIV_COUNT;
			game_message(names[g_textureSubdivision]);
		}
		break;
''', ''),
    ('\t\t"PF2 Res.  PF3 View  PF4 Debug",\n',
     '\t\t"F2 Graphics: Low/Med/High/Very Low",\n'),
    ('\t\t"PF10 Near texture quality",\n', '\t\t"PF4 Debug",\n'),
    ('''		gfx_text_shadow(g_fb,30,24+i*14,lines[i],i==0 ? C_YELLOW : C_WHITE,C_BLACK);
	}
}
''', '''		gfx_text_shadow(g_fb,30,24+i*14,lines[i],i==0 ? C_YELLOW : C_WHITE,C_BLACK);
	}
	gfx_text_shadow(g_fb,132,206,dos_graphics_message(graphicsPresetSel),C_WHITE,C_BLACK);
}
'''),
)


def adapt(name, data):
    """Return bytes and replayable count-one edits; reject changed anchors.

    LF and CRLF inputs retain their original line endings, including in the
    recipe. Mixed endings fail closed instead of silently normalizing code.
    Names other than the exact selected basename game.c are untouched.
    """
    if name != 'game.c':
        return data, []
    text = data.decode('utf-8')
    if '\r\n' in text:
        if '\n' in text.replace('\r\n', ''):
            raise ValueError('game.c has mixed line endings')
        ending = '\r\n'
    else:
        ending = '\n'
    if '\r' in text.replace('\r\n', ''):
        raise ValueError('game.c has unsupported bare CR line endings')
    recipe = []
    for before, after in REPLACEMENTS:
        before = before.replace('\n', ending)
        after = after.replace('\n', ending)
        count = text.count(before)
        if count != 1:
            raise ValueError(f'game.c: expected exactly one {before!r}; found {count}')
        text = text.replace(before, after, 1)
        recipe.append(dict(operation='replace', before=before, after=after, count=1))
    return text.encode('utf-8'), recipe
