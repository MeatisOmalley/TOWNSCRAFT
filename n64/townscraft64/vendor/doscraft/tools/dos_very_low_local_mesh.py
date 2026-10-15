"""Keep the shared 80x50 background mesh budget near its visible radius.

Only defer light/seam mesh refreshes outside the visible radius plus two
blocks. Player geometry edits, full drains, lighting, storage and simulation
retain their existing behavior. A newly approaching mesh is refreshed before
entering the view.
"""
from build_imported import replace_once


def adapt(name, data):
    if name not in ('world.c', 'game.c'):
        return data, []
    recipe = []
    text = data.decode('utf-8').replace('\r\n', '\n')
    if b'\r\n' in data:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=data.count(b'\r\n')))
    text = replace_once(text, '#include "render.h"\n',
        '#include "render.h"\n#include "graphics_presets.h"\n', recipe)
    if name == 'world.c':
        text = replace_once(text, '#include "world.h"\n',
            '#include "world.h"\n#include "player.h"\n', recipe)
        text = replace_once(text, '\tint drain=maxLightOnly>100;\n',
            '\tint drain=maxLightOnly>100;\n'
            '\t/* A sliced seam job that moved offscreen must not pin streaming. */\n'
            '\tif(!drain && dos_potato_mode && meshJobCx>=0 &&\n'
            '\t   column_dist2(meshJobCx,meshJobCz,g_player.body.x/FU,g_player.body.z/FU)>\n'
            '\t   (g_viewDist+2)*(g_viewDist+2)) mesh_cancel();\n', recipe)
        text = replace_once(text, '\t\tif(cx<0 || cz<0) continue;\n',
            '\t\tif(cx<0 || cz<0) continue;\n'
            '\t\t/* Defer offscreen light/seam meshes; geometry edits still run first. */\n'
            '\t\tif(!drain && dos_potato_mode && c->dirty!=DIRTY_GEOMETRY &&\n'
            '\t\t   column_dist2(cx,cz,g_player.body.x/FU,g_player.body.z/FU)>\n'
            '\t\t   (g_viewDist+2)*(g_viewDist+2)) continue;\n', recipe)
    else:
        text = replace_once(text,
            '\tworld_stream(g_player.body.x/FU,g_player.body.z/FU,g_viewDist+STREAM_MARGIN,maxBuild);\n',
            '\tint margin=dos_potato_mode ? 2 : STREAM_MARGIN;\n'
            '\tworld_stream(g_player.body.x/FU,g_player.body.z/FU,g_viewDist+margin,maxBuild);\n', recipe)
    return text.encode('utf-8'), recipe
