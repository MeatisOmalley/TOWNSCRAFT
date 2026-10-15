"""Optional subdivision ceiling for the shared 80x50 resolution."""
from build_imported import replace_once


def adapt(name, data):
    if name != 'render.c':
        return data, []
    text = data.decode('utf-8').replace('\r\n', '\n')
    recipe = []
    if b'\r\n' in data:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=data.count(b'\r\n')))
    text = replace_once(text, '#include "render.h"\n',
        '#include "render.h"\n#include "graphics_presets.h"\n', recipe)
    anchor = 'static void draw_quad_split(int x,int y,int z,int dir,int ex,int ez,int tex,int light)\n{\n'
    text = replace_once(text, anchor, anchor +
        '\t/* Keep small affine pieces; cutouts and proof passes retain their path. */\n'
        '\tif(dos_potato_mode && !occCapture && !occQuery &&\n'
        '\t   !texTransparent[tex] && ex<=2 && ez<=2)\n'
        '\t{\n\t\tdraw_quad(x,y,z,dir|8,ex,ez,tex,light);\n\t\treturn;\n\t}\n', recipe)
    return text.encode('utf-8'), recipe
