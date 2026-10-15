"""Planar row fills without per-row library calls.

raster_fill_rows() clears the sky and fog bands every frame. On the planar
Low/Medium target each logical row is two 80-byte plane rows 160 bytes
apart, and each was a separate out-of-line memset (-fno-builtin), 200 calls
a frame. Store the same 20 dwords per plane row inline instead; the bytes
written are identical.
"""
from build_imported import replace_once


def adapt(name, data):
    if name != 'raster.c':
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text,
        'void raster_fill_rows(int y0,int y1,u8 color)\n'
        '{\n'
        '\tint y;\n'
        '\tfor(y=MAX(0,y0); y<MIN(rh,y1); ++y)\n'
        '\t{\n'
        '\t\tif(rplanar)\n'
        '\t\t{\n'
        '\t\t\tmemset(rbuf+y*PLANAR_ROW,color,rw/2);\n'
        '\t\t\tmemset(rbuf+y*PLANAR_ROW+PLANAR_C,color,rw/2);\n'
        '\t\t}\n',
        '/* One 80-byte planar row (rw/2 with the planar target\'s 160-pixel rows). */\n'
        'static inline void fill_plane_row(u8 *dst,u32 word)\n'
        '{\n'
        '\tint n=SCR_W/16;\n'
        '\t__asm__ volatile("rep stosl":"+D"(dst),"+c"(n):"a"(word):"memory");\n'
        '}\n\n'
        'void raster_fill_rows(int y0,int y1,u8 color)\n'
        '{\n'
        '\tint y;\n'
        '\tu32 word=color*0x01010101u;\n'
        '\tfor(y=MAX(0,y0); y<MIN(rh,y1); ++y)\n'
        '\t{\n'
        '\t\tif(rplanar)\n'
        '\t\t{\n'
        '\t\t\tfill_plane_row(rbuf+y*PLANAR_ROW,word);\n'
        '\t\t\tfill_plane_row(rbuf+y*PLANAR_ROW+PLANAR_C,word);\n'
        '\t\t}\n', recipe)
    return text.encode('utf-8'), recipe
