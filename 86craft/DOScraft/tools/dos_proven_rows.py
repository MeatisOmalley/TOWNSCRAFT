"""Exact scale-2 view-row proof for the DOS VGA production packer."""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'game.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text, '#include "gfx.h"\n',
                        '#include "gfx.h"\n#include "video_backend.h"\n', recipe)
    text = replace_once(text, '\t\tswitch(state)\n\t\t{\n\t\tcase GS_INVENTORY:',
                        '\t\tint provenPlayFrame=state==GS_PLAY;\n\t\tswitch(state)\n\t\t{\n\t\tcase GS_INVENTORY:', recipe)
    text = replace_once(text, '\t\tgfx_present();\n\t\tif(syncPages)',
        '''\t\t/* render_frame has doubled every 3D view pixel and raster_finish
           has copied each even view row to its odd partner. Gameplay overlays
           below are the only later writers to that view. If another screen or
           rain is active, leave the producer proof absent. */
        if(provenPlayFrame && state==GS_PLAY && g_renderScale==2 && rainLevel<=0)
        {
            dos_video_prove_low_rows(0,200);
#ifdef DOS_VGA_PROVEN_VERTICAL
            dos_video_prove_low_row_pairs(0,200);
#endif
            dos_video_unprove_low_rows(96,105); /* Sharp crosshair. */
            if(mineProgress>0 && mineX>=0) dos_video_unprove_low_rows(110,114);
            if(g_ticks<itemNameUntil) dos_video_unprove_low_rows(188,200);
            if(g_ticks<msgUntil) dos_video_unprove_low_rows(176,188);
            if(showDebug) dos_video_unprove_low_rows(0,96);
        }
\t\tgfx_present();
\t\tif(syncPages)''', recipe)
    return text.encode('utf-8'), recipe
