"""Resolution-selected DOS art, with one atlas and an unchanged palette."""
from build_imported import replace_once, lf_text


def adapt(name, data):
    if name not in ('textures.c', 'textures.h', 'render.c', 'render.h'):
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    if name == 'textures.h':
        text = replace_once(text, 'void textures_init(void);',
            'void textures_init(void);\n/* Return nonzero only when the resident art changes. */\n'
            'int textures_set_resolution(int scale);', recipe)
        return text.encode(), recipe
    if name == 'render.h':
        text = replace_once(text, 'void render_init(void);',
            'void render_init(void);\nvoid render_sync_texture_resolution(void);', recipe)
        return text.encode(), recipe
    if name == 'render.c':
        # Reuse the original metadata scan; never reallocate renderer arenas.
        start = text.index('\tfor(t=0; t<NUM_TEXTURES; ++t)\n')
        end = text.index('\n}\n', start)
        scan = text[start:end]
        text = replace_once(text, scan, '\tdos_refresh_texture_metadata();', recipe)
        text = replace_once(text, 'void render_init(void)\n',
            '#ifdef DOS_FAR_FIELD\nstatic void far_texture_changed(void);\n#endif\n'
            'static void dos_refresh_texture_metadata(void)\n{\n\tint t,i;\n' + scan + '\n}\n\n'
            'void render_sync_texture_resolution(void)\n{\n'
            '\tif(!textures_set_resolution(g_renderScale)) return;\n'
            '\tdos_refresh_texture_metadata();\n'
            '#ifdef DOS_FAR_FIELD\n\tfar_texture_changed();\n#endif\n}\n\n'
            'void render_init(void)\n', recipe)
        text = replace_once(text, '\toccQuery=0; g_statOccluded=0;\n',
            '\t/* Covers benchmark/direct scale changes too; no per-texel dispatch. */\n'
            '\trender_sync_texture_resolution();\n\toccQuery=0; g_statOccluded=0;\n', recipe)
        return text.encode(), recipe
    text = replace_once(text, '/* ---- block textures ---- */\n',
        'extern int g_renderScale;\nstatic int dos_texture_detail=-1;\n'
        '#include "block_textures.inc"\n\n/* ---- block textures ---- */\n', recipe)
    # Native texture previews and game builds both include the new mob art.
    if 'begin(T_CHICKEN_SKIN)' not in text:
        import dos_chickens
        updated,steps=dos_chickens.adapt('textures.c',text.encode())
        text=updated.decode();recipe.extend(steps)
    shared = {'gen_stone', 'gen_gravel', 'gen_cobble', 'gen_craft_top', 'gen_craft_side',
              'gen_ore', 'gen_wool', 'gen_stonebrick'}
    for name, args, call in (
        ('gen_stone', 'void', 'dos_tex_stone()'),
        ('gen_dirt', 'void', 'dos_tex_dirt()'),
        ('gen_cobble', 'void', 'dos_tex_cobble()'),
        ('gen_planks', 'int ramp,int dark', 'dos_tex_planks(ramp,dark)'),
        ('gen_log_side', 'void', 'dos_tex_log_side()'),
        ('gen_log_top', 'void', 'dos_tex_log_top()'),
        ('gen_grass_side', 'void', 'dos_tex_grass_side()'),
        ('gen_gravel', 'void', 'dos_tex_gravel()'),
        ('gen_craft_top', 'void', 'dos_tex_craft_top()'),
        ('gen_craft_side', 'int front', 'dos_tex_craft_side(front)'),
        ('gen_ore', 'int ramp,int shade', 'dos_tex_ore(ramp,shade)'),
        ('gen_wool', 'void', 'dos_tex_wool()'),
        ('gen_stonebrick', 'void', 'dos_tex_stonebrick()'),
    ):
        begin = text.index('static void '+name+'('+args+')\n')
        end = text.index('\n}\n', begin)+3
        original = text[begin:end]
        if name in shared:
            replacement = 'static void '+name+'('+args+')\n{\n\t'+call+';\n}\n'
        else:
            replacement = original.replace('{\n',
                '{\n\tif(dos_texture_detail) { '+call+'; return; }\n', 1)
            if name == 'gen_dirt':
                # Retain the three newer browns, but keep original sparse coverage.
                replacement = replacement.replace('base_specks(R_DIRT,12,10,16,14,6);',
                    'base_specks(R_DIRT,14,13,16,15,6);')
        text = replace_once(text, original, replacement, recipe)
    for before, after in (
        ('begin(T_GRASS_TOP); base_specks(R_GRASS,12,11,22,13,6);',
         'begin(T_GRASS_TOP); if(dos_texture_detail) dos_tex_grass_top(); else base_specks(R_GRASS,12,11,22,13,6);'),
        ('begin(T_LEAVES); base_specks(R_LEAF,11,8,22,13,10);',
         'begin(T_LEAVES); if(dos_texture_detail) dos_tex_leaves(); else base_specks(R_LEAF,11,10,22,12,10);'),
        ('begin(T_SAND); base_specks(R_SAND,13,12,14,11,5);',
         'begin(T_SAND); base_specks(R_SAND,15,14,8,15,0);'),
    ):
        text = replace_once(text, before, after, recipe)
    start=text.index('\tbegin(T_WATER); noisefill(R_WATER,11,2);')
    end=text.index('\tbegin(T_GLASS); gen_glass();',start)
    text=replace_once(text,text[start:end],'\tbegin(T_WATER); dos_tex_water();\n',recipe)
    start=text.index('static void gen_furnace(int face)\n')
    end=text.index('\n}\n',start)+3
    before=text[start:end]
    text=replace_once(text,before,before[:-3]+'\n\tif(face==1) dos_tex_furnace_fire();\n}\n',recipe)
    text=replace_once(text,'void textures_init(void)\n',
        '#include "../../../assets/textures/eagler_full.inc"\n'
        '#include "../../../assets/textures/eagler_simple.inc"\n\nstatic void dos_textures_build(void)\n',recipe)
    text=replace_once(text,'\t/* Shaded copies of world textures, one atlas page per texture.  Pages\n',
        '\tif(dos_texture_detail) dos_tex_import_full();\n'
        '\telse dos_tex_import_simple();\n\n'
        '\t/* Shaded copies of world textures, one atlas page per texture.  Pages\n',recipe)
    text=replace_once(text,
        '\tg_texAtlas=(u8 *)(((u32)heap_alloc_low(NUM_SHADED_TEXTURES*4096+4095)+4095)&~4095u);',
        '\tif(!g_texAtlas)\n\t\tg_texAtlas=(u8 *)(((u32)heap_alloc_low(NUM_SHADED_TEXTURES*4096+4095)+4095)&~4095u);',recipe)
    text=replace_once(text,'/* Average color of a texture (for tiny distant faces) */',
        'void textures_init(void)\n{\n\tdos_texture_detail=g_renderScale==1;\n\tdos_textures_build();\n}\n\n'
        'int textures_set_resolution(int scale)\n{\n'
        '\tint detail=scale==1;\n\tif(detail==dos_texture_detail) return 0;\n'
        '\tdos_texture_detail=detail;\n\tdos_textures_build();\n\treturn 1;\n}\n\n'
        '/* Average color of a texture (for tiny distant faces) */',recipe)
    return text.encode('utf-8'), recipe
