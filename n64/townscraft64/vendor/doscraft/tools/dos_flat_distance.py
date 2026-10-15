"""Shared texture-distance policy and flat-face subdivision avoidance."""
from build_imported import replace_once


def adapt(name, data):
    if name != 'render.c':
        return data, []
    recipe = []
    text = data.decode('utf-8').replace('\r\n', '\n')
    if b'\r\n' in data:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=data.count(b'\r\n')))
    text = replace_once(text, '/* Blocks: farther faces are flat shaded (0 = off) */',
        '/* -1: textureless opaque blocks; 0: all textured; positive: cutoff */', recipe)
    text = replace_once(text, '\tez=(dir<=DIR_PY) ? h : 1;\n',
        '\tez=(dir<=DIR_PY) ? h : 1;\n'
        '\t/* Decide before subdivision: a solid-color face needs no affine pieces.\n'
        '\t   Textureless mode retains cutout masks for foliage and other geometry. */\n'
        '\tint flatFace=g_flatDist<0 && !texTransparent[tex];\n'
        '\tif(g_flatDist>0 && (!texTransparent[tex] || T_LEAVES==tex))\n'
        '\t{\n'
        '\t\tint nx=CLAMP(camBX,x,x+ex-1)-camBX,nz=CLAMP(camBZ,z,z+ez-1)-camBZ;\n'
        '\t\tflatFace=sqTab[ABS(nx)]+sqTab[ABS(nz)]>g_flatDist*g_flatDist;\n'
        '\t}\n', recipe)
    text = replace_once(text, 'if(split && g_textureSubdivision!=SUBDIV_OFF &&',
        'if(split && (!flatFace || occCapture || occQuery) && g_textureSubdivision!=SUBDIV_OFF &&', recipe)
    text = replace_once(text, '\tconst u8 *tile=TEX_TILE(tex,L);\n',
        '\tconst u8 *tile=flatFace ? NULL : TEX_TILE(tex,L);\n', recipe)
    start = text.index('\tint flags=texTransparent[tex]|((ex>1 || ez>1) ? RP_WRAP : 0);\n')
    end = text.index('\tCVert cv[4];\n', start)
    text = replace_once(text, text[start:end],
        '\tint flags=flatFace ? 0 : texTransparent[tex]|((ex>1 || ez>1) ? RP_WRAP : 0);\n', recipe)
    return text.encode('utf-8'), recipe
