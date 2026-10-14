"""Very Low visual tier: plants as one camera-facing upright quad.

Flowers and tall grass are two crossed, double-sided quads (draw_cross).
At Very Low resolutions (80x100 and Potato), draw one quad through the cell's centre that
always faces the camera instead. The camera has no roll, so its right axis
is camera-space x: the quad is the cell's centre column widened along x,
keeping the full cell height and the plant texture. Plants look the same
from every side and lose their crossed depth when circled. Other presets
keep the crossed quads.
"""
from build_imported import replace_once

# Half width in world units (256 per block): 14/16 of a block across, the
# crossed quads' apparent width when seen along an axis.
BILLBOARD = '''#ifdef DOS_VERY_LOW
extern int dos_very_low_mode;
#endif

static void draw_cross(int bx,int by,int bz,int texId,int L)
{
#ifdef DOS_VERY_LOW
	if(dos_very_low_mode)
	{
		/* One upright camera-facing quad: the cell's centre column widened
		   along the camera's (roll-free) right axis, camera-space x. */
		CVert bb[4];
		int k,c,base[3];
		for(c=0; c<3; ++c)
		{
			base[c]=TX[bx].c[c]+TY[by].c[c]+TZ[bz].c[c]+AX16[8].c[c]+AZ16[8].c[c];
		}
		for(k=0; k<4; ++k)
		{
			const V3 *up=&AY16[k<2 ? 16 : 0];
			for(c=0; c<3; ++c)
			{
				(&bb[k].x)[c]=base[c]+up->c[c];
			}
			bb[k].x+=(k==1 || k==2) ? 112 : -112;
			bb[k].u=uvc((k==1 || k==2) ? 16 : 0);
			bb[k].v=uvc((k>=2) ? 16 : 0);
		}
		draw_cpoly(bb,4,TEX_TILE(texId,L),1,0);
		return;
	}
#endif
'''


def adapt(name, data):
    if name != 'render.c':
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text, 'static void draw_cross(int bx,int by,int bz,int texId,int L)\n{\n',
                        BILLBOARD, recipe)
    return text.encode('utf-8'), recipe
