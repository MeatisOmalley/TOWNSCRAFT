"""Presentation-only first-person viewmodel. Pinned sources stay immutable."""
from build_imported import replace_once,lf_text

def adapt(name,data):
    if name not in ('game.c','render.c'):return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    text=replace_once(text,'#include "bench.h"\n','#include "bench.h"\n#include "viewmodel.h"\n',recipe)
    if name=='game.c':
        text=replace_once(text,'\trender_frame(g_fb,&env);\n',
            '\t/* Foreground visual state only; no inventory/gameplay mutation. */\n'
            '\t{\n\t\tint item=g_inv[g_player.selected].count ? held_item() : 0;\n'
            '\t\tint light=world_light_at(ex/FU,ey/FU,ez/FU);\n'
            '\t\tlight=MAX((light>>4)-g_skyDarken,light&15);\n'
            '\t\tdos_viewmodel_update(state==GS_PLAY && !g_player.dead,item,light,\n'
            '\t\t\tg_player.swing,mineProgress>0,g_player.body.onGround &&\n'
            '\t\t\t(ABS(g_player.body.vx)+ABS(g_player.body.vz)>60),ex,ez,g_ticks);\n\t}\n'
            '\trender_frame(g_fb,&env);\n',recipe)
    else:
        text=replace_once(text,'\tdraw_target_outline(env);\n',
            '\tdraw_target_outline(env);\n\t/* Foreground draws through the same raster/row-proof path, before HUD. */\n'
            '\tdos_viewmodel_draw(vw,vh);\n',recipe)
    return text.encode(),recipe
