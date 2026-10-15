"""Pass the existing per-page HUD redraw proof to the DOS presenter."""

from build_imported import replace_once


def adapt(name, data):
    if name not in ('ui.c', 'game.c'):
        return data, []
    recipe = []
    text = data.decode('utf-8')
    if name == 'ui.c':
        text = replace_once(text, '#include "ui.h"\n',
                            '#include "ui.h"\n#include "video_backend.h"\n', recipe)
        text = replace_once(text,
            '\tif(hudValid[page] && 0==memcmp(&k,&hudKey[page],sizeof(k)))\n'
            '\t{\n\t\treturn;\n\t}\n',
            '\tif(hudValid[page] && 0==memcmp(&k,&hudKey[page],sizeof(k)))\n'
            '\t{\n\t\tdos_video_prove_hud_unchanged(page);\n\t\treturn;\n\t}\n', recipe)
    else:
        text = replace_once(text, '\t\t\t\tif(inMenu)\n'
            '\t\t\t\t{\n\t\t\t\t\tgfx_darken(g_fb,0,0,SCR_W,SCR_H);\n',
            '\t\t\t\tif(inMenu)\n'
            '\t\t\t\t{\n\t\t\t\t\tdos_video_unprove_hud();\n'
            '\t\t\t\t\tgfx_darken(g_fb,0,0,SCR_W,SCR_H);\n', recipe)
        text = replace_once(text, '#include "gfx.h"\n',
                            '#include "gfx.h"\n#include "video_backend.h"\n', recipe)
    return text.encode('utf-8'), recipe
