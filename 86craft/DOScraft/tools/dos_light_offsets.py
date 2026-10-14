"""Light flood fill: step to neighbors by fixed offsets inside a column.

propagate() and remove_light() decoded every queued cell and then called
widx() for all six neighbors (column-slot lookup, divides and multiplies).
Inside one 16x16 column, x/y/z neighbors are exactly i+-WH, i+-1 and
i+-CS*WH (the flat world uses i+-strideZ), the same relation light_dirty()
already relies on. widx() is now only called when a step crosses a column
seam. Visit order, bounds and resulting light values are unchanged.
"""
from build_imported import replace_once

HELPER = '''
/* Same result as in_world()+widx() for the six flood-fill directions, but
   resolves a column slot only when the step leaves the current column. */
static inline u32 light_neighbor(u32 i,int x,int y,int z,int d)
{
	int rowStride=g_columnMap ? CS*WH : strideZ;
	switch(d)
	{
	case 0: if(x<=0) return ~0u; return !g_columnMap || (x&15) ? i-WH : (u32)widx(x-1,y,z);
	case 1: if(x>=g_W-1) return ~0u; return !g_columnMap || (x&15)<15 ? i+WH : (u32)widx(x+1,y,z);
	case 2: return y>0 ? i-1 : ~0u;
	case 3: return y<WH-1 ? i+1 : ~0u;
	case 4: if(z<=0) return ~0u; return !g_columnMap || (z&15) ? i-rowStride : (u32)widx(x,y,z-1);
	default: if(z>=g_W-1) return ~0u; return !g_columnMap || (z&15)<15 ? i+rowStride : (u32)widx(x,y,z+1);
	}
}

static void propagate(int ch)
'''


def adapt(name, data):
    if name != 'world.c':
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, '\nstatic void propagate(int ch)\n', HELPER, recipe)
    lookup = ('\t\t\tif(!in_world(nx,ny,nz))\n'
              '\t\t\t{\n'
              '\t\t\t\tcontinue;\n'
              '\t\t\t}\n'
              '\t\t\tn=widx(nx,ny,nz);\n'
              '\t\t\tif((int)n<0) continue;\n')
    fast = ('\t\t\tn=light_neighbor(i,x,y,z,d);\n'
            '\t\t\tif((int)n<0) continue;\n')
    text = replace_once(text, lookup + '\t\t\top=g_lightOpacity', fast + '\t\t\top=g_lightOpacity', recipe)
    text = replace_once(text, lookup + '\t\t\tnl=lget(n,ch);', fast + '\t\t\tnl=lget(n,ch);', recipe)
    return text.encode('utf-8'), recipe
