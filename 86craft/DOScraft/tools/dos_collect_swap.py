"""Keep the wide collection planes in their own arrays.

With DOS_COLLECT_MARGIN, every wide re-collection rewrote side planes 1-4
of PX/PY/PZ with collect_planes(1) and then recomputed the tight planes with
collect_planes(0). The wide planes now go to separate arrays; restoring the
tight planes is a pointer swap that returns setup_camera()'s exact values,
instead of a second pass of four multiplies per grid line.
"""
from build_imported import replace_once

DECLS = '''static int *PX[NPLANES],*PY[NPLANES],*PZ[NPLANES];
/* Wide planes live in separate arrays; restoring the tight ones is a swap. */
static int *PXw[NPLANES],*PYw[NPLANES],*PZw[NPLANES];
static int *PXt[NPLANES],*PYt[NPLANES],*PZt[NPLANES],planeLenT[NPLANES];
'''

HELPERS = '''
static void collect_planes_wide(void)
{
	for(int k=1;k<NPLANES;++k) {
		PXt[k]=PX[k]; PYt[k]=PY[k]; PZt[k]=PZ[k]; planeLenT[k]=planeLen[k];
		PX[k]=PXw[k]; PY[k]=PYw[k]; PZ[k]=PZw[k];
	}
	collect_planes(1);
}

static void collect_planes_tight(void)
{
	for(int k=1;k<NPLANES;++k) {
		PX[k]=PXt[k]; PY[k]=PYt[k]; PZ[k]=PZt[k]; planeLen[k]=planeLenT[k];
	}
}

/* A steep downward view'''


def adapt(name, data):
    if name != 'render.c':
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, 'static int *PX[NPLANES],*PY[NPLANES],*PZ[NPLANES];\n', DECLS, recipe)
    text = replace_once(text, '\n/* A steep downward view', HELPERS, recipe)
    text = replace_once(text,
        '\t\tPY[i]=heap_alloc_low(sizeof(int)*(WH+2));\n',
        '\t\tPY[i]=heap_alloc_low(sizeof(int)*(WH+2));\n'
        '\t\tif(i)\n'
        '\t\t{\n'
        '\t\t\tPXw[i]=heap_alloc_low(sizeof(int)*(g_W+2));\n'
        '\t\t\tPZw[i]=heap_alloc_low(sizeof(int)*(g_W+2));\n'
        '\t\t\tPYw[i]=heap_alloc_low(sizeof(int)*(WH+2));\n'
        '\t\t}\n', recipe)
    text = replace_once(text, 'if(collectWide) collect_planes(1);',
                        'if(collectWide) collect_planes_wide();', recipe)
    text = replace_once(text, 'collectWide=0; collect_planes(0);',
                        'collectWide=0; collect_planes_tight();', recipe)
    text = replace_once(text, 'if(collectWide) collect_planes(0);',
                        'if(collectWide) collect_planes_tight();', recipe)
    return text.encode('utf-8'), recipe
