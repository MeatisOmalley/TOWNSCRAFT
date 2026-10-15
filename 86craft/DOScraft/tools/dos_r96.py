"""1996 renderer overlay: perspective data for the Pentium/Voodoo backends.

The shared visibility front end (collection, painter's order, culling) stays
the release render.c. This overlay only makes each projected vertex carry its
reciprocal depth, so the replacement rasterizers can map textures with exact
perspective instead of the 486 renderer's screen-affine spans:

- RVert gains w, the projection's own focal16<<16/z (the value that already
  scales x and y, so w is exactly consistent with the screen position).
- Polygons from draw_quad/draw_cpoly are flagged RP_PERSP. Direct raster
  callers (viewmodel, far field) keep the affine contract with w unused.
- The guard-band clip interpolates w linearly and u,v perspective-correctly.
- The draw list holds more items, for the longer 1996 view distances.
- Atlas rebuilds notify the backend (r96_textures_changed).

Every replacement fails closed, as in dos_sources.
"""

from build_imported import replace_once, lf_text

CLIP_UV = '''/* R96: guard-band clip in screen space. 1/z is linear on screen, u and v
   are linear only after multiplying by it. Rare (huge polygons only). */
static void r96_clip_uv(const RVert *a,const RVert *b,int t,RVert *o)
{
	long long wa=((long long)a->w*(65536-t))>>16,wb=((long long)b->w*t)>>16,den=wa+wb;
	if(den<=0)
	{
		o->u=a->u+mulshift(b->u-a->u,t,16);
		o->v=a->v+mulshift(b->v-a->v,t,16);
		return;
	}
	o->u=(int)(((long long)a->u*wa+(long long)b->u*wb)/den);
	o->v=(int)(((long long)a->v*wa+(long long)b->v*wb)/den);
}

/* Clip polygon against one screen edge.'''


def adapt(name, data):
    if name not in ('raster.h', 'render.c'):
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    if name == 'raster.h':
        text = replace_once(text, '\tint u,v;   /* 16.16 texels */\n} RVert;',
            '\tint u,v;   /* 16.16 texels */\n'
            '\tint w;     /* R96: focal16<<16/z of the projection; read with RP_PERSP only */\n'
            '} RVert;', recipe)
        text = replace_once(text, '#define RP_WRAP        2   /* texture coordinates repeat (may leave 0..16) */\n',
            '#define RP_WRAP        2   /* texture coordinates repeat (may leave 0..16) */\n'
            '#define RP_PERSP       4   /* R96: vertices carry w; map with exact perspective */\n', recipe)
        return text.encode('utf-8'), recipe
    text = replace_once(text, 'static inline void project(int x,int y,int z,int *sx,int *sy)\n{\n',
        '/* R96: the reciprocal used by the latest projection, for RVert.w. */\n'
        'static int r96Inv;\n\n'
        'static inline void project(int x,int y,int z,int *sx,int *sy)\n{\n', recipe)
    text = replace_once(text, '\t*sx=cx16+mulshift(x,inv,16);\n\t*sy=cy16-mulshift(y,inv,16);\n}\n',
        '\tr96Inv=inv;\n\t*sx=cx16+mulshift(x,inv,16);\n\t*sy=cy16-mulshift(y,inv,16);\n}\n', recipe)
    text = replace_once(text, '/* Clip polygon against one screen edge.', CLIP_UV, recipe)
    text = replace_once(text,
        '\t\t\to->u=a->u+mulshift(b->u-a->u,t,16);\n\t\t\to->v=a->v+mulshift(b->v-a->v,t,16);\n\t\t\tif(axis)\n',
        '\t\t\to->w=a->w+mulshift(b->w-a->w,t,16);\n\t\t\tr96_clip_uv(a,b,t,o);\n\t\t\tif(axis)\n', recipe)
    text = replace_once(text, '\t\traster_poly(sv,m,tex,flags);\n',
        '\t\traster_poly(sv,m,tex,flags|RP_PERSP);\n', recipe)
    text = replace_once(text, '\t\tsv[i].u=cv[i].u;\n', '\t\tsv[i].w=r96Inv;\n\t\tsv[i].u=cv[i].u;\n', recipe)
    text = replace_once(text, '\t\t\tsv[k].u=cv[k].u;\n', '\t\t\tsv[k].w=r96Inv;\n\t\t\tsv[k].u=cv[k].u;\n', recipe)
    # A backend that keeps textures elsewhere (the Voodoo's TMU) re-sends
    # them whenever the atlas is rebuilt.
    text = replace_once(text, '\t\ttexAvg[t]=texture_average(t);\n\t}\n}\n',
        '\t\ttexAvg[t]=texture_average(t);\n\t}\n\tr96_textures_changed();\n}\n', recipe)
    text = replace_once(text, 'static void dos_refresh_texture_metadata(void)\n',
        'void r96_textures_changed(void);\nstatic void dos_refresh_texture_metadata(void)\n', recipe)
    # A 16-block view collects roughly four times the 8-block list; order
    # indices are u16, so 32000 stays well inside them.
    text = replace_once(text, '\treturn g_ramMB>=4 ? 8000 : 3000;\n',
        '\treturn g_ramMB>=32 ? 32000 : g_ramMB>=4 ? 8000 : 3000;\n', recipe)
    return text.encode('utf-8'), recipe
