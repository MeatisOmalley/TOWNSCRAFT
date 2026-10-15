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

from build_imported import replace_once, lf_text


def adapt(name, data, *, far_field=False):
    if name != 'raster.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)

    def one(before, after):
        nonlocal text
        text = replace_once(text, before, after, recipe)

    one('static u8 *planarFb,*planarSrc;\n',
        'static u8 *planarFb,*planarSrc;\n'
        '/* Planar frame of a Very Low resolution: rows with dos_very_low_rows bit 0\n'
        '   hold only the A bytes (see low_planar.S, VL loops). Such a frame sets\n'
        '   rplanar to 2. */\n'
        'static int planarVL;\n'
        'void trap_opaquePv(Trap *t);\nvoid trap_transpPv(Trap *t);\n'
        'void trap_opaquePwv(Trap *t);\nvoid trap_transpPwv(Trap *t);\n'
        'static void (*const trapFuncPlanarVL[4])(Trap *)=\n{\n'
        '\ttrap_opaquePv,trap_transpPv,trap_opaquePwv,trap_transpPwv,\n};\n')
    one('\tflat_fill(row+PLANAR_C+c,(xr>>1)-c,word);\n}\n',
        '\tflat_fill(row+PLANAR_C+c,(xr>>1)-c,word);\n}\n\n'
        '/* Very Low planar row: the A bytes (even logical pixels) only. */\n'
        'static inline void planar_span_a(u8 *row,int xl,int xr,u8 color)\n{\n'
        '\tint a=(xl+1)>>1;\n'
        '\tflat_fill(row+a,((xr+1)>>1)-a,(u32)color*0x01010101u);\n}\n')
    # Very Low planar frames get their own flat loop; the others keep theirs.
    one('\t\t\telse\n\t\t\t{\n\t\t\t\tif(g_fastFlat)\n',
        '\t\t\telse if(rplanar>1)\n\t\t\t{\n'
        '\t\t\t\tfor(; s<end; ++s,row+=stride)\n\t\t\t\t{\n'
        '\t\t\t\t\tint xl=(left->x16+0x7FFF)>>16,xr=(right->x16+0x7FFF)>>16;\n'
        '\t\t\t\t\tif(xr>rw) xr=rw;\n'
        '\t\t\t\t\tif(xl<0) xl=0;\n'
        '\t\t\t\t\tif(xr>xl && !(dos_very_low_rows[s]&2))\n\t\t\t\t\t{\n'
        '\t\t\t\t\t\tBENCH_HOOK(if(!g_fastFlat) g_benchCnt[S_FLATPX]+=xr-xl);\n'
        '\t\t\t\t\t\tif(dos_very_low_rows[s]&1) planar_span_a(row,xl,xr,flat);\n'
        '\t\t\t\t\t\telse planar_span(row,xl,xr,flat);\n'
        '\t\t\t\t\t}\n'
        '\t\t\t\t\tleft->x16+=left->dx;\n'
        '\t\t\t\t\tright->x16+=right->dx;\n'
        '\t\t\t\t}\n\t\t\t}\n'
        '\t\t\telse\n\t\t\t{\n\t\t\t\tif(g_fastFlat)\n')
    one('rplanar ? trapFuncPlanar[flags&(RP_TRANSPARENT|RP_WRAP)]',
        'rplanar ? (rplanar>1 ? trapFuncPlanarVL : trapFuncPlanar)[flags&(RP_TRANSPARENT|RP_WRAP)]')
    one('\tu32 word=color*0x01010101u;\n\tfor(y=MAX(0,y0); y<MIN(rh,y1); ++y)\n',
        '\tu32 word=color*0x01010101u;\n'
        '\tif(rplanar>1)\n\t{\n'
        '\t\t/* Very Low rows take only A bytes; Potato repeats odd rows. */\n'
        '\t\tfor(y=MAX(0,y0); y<MIN(rh,y1); ++y)\n\t\t{\n'
        '\t\t\tif(dos_very_low_rows[y]&2) continue;\n'
        '\t\t\tfill_plane_row(rbuf+y*PLANAR_ROW,word);\n'
        '\t\t\tif(!(dos_very_low_rows[y]&1)) fill_plane_row(rbuf+y*PLANAR_ROW+PLANAR_C,word);\n'
        '\t\t}\n\t\treturn;\n\t}\n'
        '\tfor(y=MAX(0,y0); y<MIN(rh,y1); ++y)\n')
    if far_field:
        one('        c->stride=PLANAR_ROW; c->mode=width==1 ? 9 : width==2 ? 10 : 12;\n',
            '        c->stride=PLANAR_ROW; c->mode=width==1 ? 9 : width==2 ? 10 : 12;\n'
            '        /* Potato: A bytes of kept rows only (far_fill.h modes 13/11). */\n'
            '        if(planarVL && dos_potato_mode && width>1 && !(x&1)) c->mode=width==2 ? 13 : 11;\n'
            '#ifdef DOS_VERY_LOW_FAR_COARSE\n'
            '        if(planarVL && dos_potato_mode && width==8 && !(x&1)) c->mode=14;\n'
            '#endif\n')
    one('\trplanar=planarPolicy && 2==scale && SCR_W/2==w && VIEW_H/2==h;\n',
        '\trplanar=planarPolicy && 2==scale && SCR_W/2==w && VIEW_H/2==h;\n'
        '\tif(rplanar && planarVL) rplanar=2;\n')
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
