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
- On 32 MiB or more (the 128 MiB 1996 machine), a 7x7 ring of resident
  terrain columns and a larger mesh pool allow views of up to 32 blocks.

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
    if name not in ('raster.h', 'render.c', 'world.c', 'column_cache.inc'):
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    if name == 'world.c':
        # 128 MiB machines keep a 7x7 ring of terrain columns resident (48
        # blocks on every side) instead of 5x5, for views beyond 16 blocks,
        # and a mesh pool for the larger meshed area.
        text = replace_once(text, 'static u8 residentDirty[25];\nstatic u8 residentReady[25];\n',
            '/* R96: up to 49 resident columns (world_alloc) */\n'
            'static u8 residentDirty[49];\nstatic u8 residentReady[49];\n', recipe)
        text = replace_once(text, 'columnCoordsStorage[50];\n', 'columnCoordsStorage[98];\n', recipe)
        text = replace_once(text, '\tg_residentColumns=25;\n',
            '\tg_residentColumns=g_ramMB>=32 ? 49 : 25;\n', recipe)
        text = replace_once(text, '\treturn g_ramMB<=2 ? 16000 : 24000;\n',
            '\treturn g_ramMB>=32 ? 64000 : g_ramMB<=2 ? 16000 : 24000;\n', recipe)
        return text.encode('utf-8'), recipe
    if name == 'column_cache.inc':
        # The streamed neighborhood and its eviction guard follow the ring.
        text = replace_once(text, 'static void cache_stream(int x,int z,int budget)\n{\n',
            '/* R96: columns kept around the player: 2 (5x5) or 3 (7x7) */\n'
            '#define R96_RING (g_residentColumns>25 ? 3 : 2)\n'
            'static void cache_stream(int x,int z,int budget)\n{\n', recipe)
        text = replace_once(text,
            '\t\t\tfor(cz=MAX(0,pcz-2); cz<=MIN(g_NC-1,pcz+2); ++cz)\n'
            '\t\t\tfor(cx=MAX(0,pcx-2); cx<=MIN(g_NC-1,pcx+2); ++cx)\n',
            '\t\t\tfor(cz=MAX(0,pcz-R96_RING); cz<=MIN(g_NC-1,pcz+R96_RING); ++cz)\n'
            '\t\t\tfor(cx=MAX(0,pcx-R96_RING); cx<=MIN(g_NC-1,pcx+R96_RING); ++cx)\n', recipe)
        text = replace_once(text,
            '\t\t\tif(ABS(cx-pcx)<=2 && ABS(cz-pcz)<=2 && (!critical || d<=26*26)) continue;\n',
            '\t\t\tif(ABS(cx-pcx)<=R96_RING && ABS(cz-pcz)<=R96_RING &&\n'
            '\t\t\t   (!critical || d<=(R96_RING*16-6)*(R96_RING*16-6))) continue;\n', recipe)
        return text.encode('utf-8'), recipe
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
