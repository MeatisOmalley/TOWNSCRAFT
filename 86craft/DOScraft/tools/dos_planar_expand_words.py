"""Expand planar overlay-band rows with one dword store per A/C byte pair.

Bytes 4x..4x+3 of the chunky row are a[x],a[x],c[x],c[x]; the little-endian
dword a*0x0101|c*0x01010000 stores the same four bytes. Rejected 2026-10-13:
GCC assembles the dword in 15 instructions against 9 for four byte stores, so
Low's finish stage grew 0.15 ms. Kept selectable only.
"""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'raster.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text,
        '    for(x=0; x<SCR_W/4; ++x)\n    {\n'
        '        dst[4*x]=dst[4*x+1]=a[x];\n        dst[4*x+2]=dst[4*x+3]=c[x];\n    }\n',
        '    for(x=0; x<SCR_W/4; ++x)\n'
        '        ((u32 *)dst)[x]=a[x]*0x0101u|c[x]*0x01010000u;\n', recipe)
    return text.encode('utf-8'), recipe
