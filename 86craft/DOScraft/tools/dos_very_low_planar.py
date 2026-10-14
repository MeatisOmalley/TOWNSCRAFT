"""Render Very Low and Potato play frames straight into Mode X plane 0.

Very Low shows one logical texel per four VGA pixels: the presenter reads
byte 4x of each chunky row and broadcasts it to all planes. With this overlay
the planar Low target is used for those frames too, and Very Low rows store
only that byte: logical pixel 2k goes to plane-0 byte k (the planar A row),
odd logical pixels are stepped over as the Very Low texel loops do. The
presenter then only repeats each A row; nothing is packed from chunky pixels.
Rows that sharp overlays touch keep the full planar A/C layout and are
expanded to chunky at finish, as in planar Low. Applied after every other
raster.c overlay.
"""

from build_imported import replace_once


def adapt(name, data):
    if name != 'raster.c':
        return data, []
    recipe = []
    text = data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n',
                           count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')

    def one(before, after):
        nonlocal text
        text = replace_once(text, before, after, recipe)

    one('static u8 *planarFb,*planarSrc;\n',
        'static u8 *planarFb,*planarSrc;\n'
        '/* Planar frame of a Very Low resolution: rows with dos_very_low_rows bit 0\n'
        '   hold only the A bytes (see low_planar.S, VL loops). */\n'
        'static int planarVL;\n'
        'void trap_opaquePv(Trap *t);\nvoid trap_transpPv(Trap *t);\n'
        'void trap_opaquePwv(Trap *t);\nvoid trap_transpPwv(Trap *t);\n'
        'static void (*const trapFuncPlanarVL[4])(Trap *)=\n{\n'
        '\ttrap_opaquePv,trap_transpPv,trap_opaquePwv,trap_transpPwv,\n};\n')
    one('static inline void planar_span(u8 *row,int xl,int xr,u8 color)\n',
        'static inline void planar_span(u8 *row,int xl,int xr,u8 color,int aOnly)\n')
    one('\tflat_fill(row+PLANAR_C+c,(xr>>1)-c,word);\n',
        '\tif(!aOnly) flat_fill(row+PLANAR_C+c,(xr>>1)-c,word);\n')
    old = 'if(rplanar) planar_span(row,xl,xr,flat);'
    if text.count(old) != 2:
        raise ValueError('Expected two planar span calls')
    new = 'if(rplanar) planar_span(row,xl,xr,flat,planarVL && (dos_very_low_rows[s]&1));'
    recipe.append(dict(operation='replace', before=old, after=new, count=2))
    text = text.replace(old, new)
    one('rplanar ? trapFuncPlanar[flags&(RP_TRANSPARENT|RP_WRAP)]',
        'rplanar ? (planarVL ? trapFuncPlanarVL : trapFuncPlanar)[flags&(RP_TRANSPARENT|RP_WRAP)]')
    one('\t\t\tfill_plane_row(rbuf+y*PLANAR_ROW+PLANAR_C,word);\n',
        '\t\t\tif(!planarVL || !(dos_very_low_rows[y]&1))\n'
        '\t\t\t\tfill_plane_row(rbuf+y*PLANAR_ROW+PLANAR_C,word);\n')
    one('    planarPolicy=skip && !dos_very_low_mode;\n',
        '    planarPolicy=skip;\n    planarVL=dos_very_low_mode;\n')
    one('    const u8 *a=planarSrc+y*PLANAR_ROW,*c=a+PLANAR_C;\n    int x;\n',
        '    const u8 *a=planarSrc+y*PLANAR_ROW,*c=a+PLANAR_C;\n    int x;\n'
        '    if(planarVL && (dos_very_low_rows[y]&1))\n    {\n'
        '        for(x=0; x<SCR_W/4; ++x) ((u32 *)dst)[x]=a[x]*0x01010101u;\n'
        '        memcpy(dst+planarFbPitch,dst,SCR_W);\n        return;\n    }\n')
    one('        /* Sharp overlays draw over the chunky framebuffer after this; give\n'
        '           them a correct background. All other rows stay planar. */\n'
        '        for(y=0; y<rh; ++y)\n',
        '        /* Potato: omitted logical rows repeat the row above. */\n'
        '        if(planarVL)\n'
        '            for(y=1; y<rh; ++y)\n'
        '                if(dos_very_low_rows[y]&2)\n'
        '                    memcpy(planarSrc+y*PLANAR_ROW,planarSrc+(y-1)*PLANAR_ROW,SCR_W/4);\n'
        '        /* Sharp overlays draw over the chunky framebuffer after this; give\n'
        '           them a correct background. All other rows stay planar. */\n'
        '        for(y=0; y<rh; ++y)\n')
    return text.encode('utf-8'), recipe
