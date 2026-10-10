"""Low/Medium adaptive leaf coarsening only for bounded low-depth-spread nodes."""
from build_imported import replace_once

HELPER='''/* Bounded quality tradeoff: at most 2x2 opaque cells, fully near-visible.
   Test actual grid-corner depths, not an assumed monotonic table bound. */
static int split_depth_safe(int x,int y,int z,int dir,int ex,int ez)
{
\tint k,lo=0x7FFFFFFF,hi=0;
\tfor(k=0;k<4;++k)
\t{
\t\tconst u8 *s=faceCorner[dir][k];
\t\tint depth=TX[x+(s[0]?ex:0)].c[2]+TY[y+s[1]].c[2]+TZ[z+(s[2]?ez:0)].c[2];
\t\tlo=MIN(lo,depth); hi=MAX(hi,depth);
\t}
\treturn lo>=NEAR_Z && hi-lo<=(lo>>4);
}

'''


def adapt(name,data):
    if name!='render.c': return data,[]
    recipe=[]; text=data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
        text=text.replace('\r\n','\n')
    anchor='/* Draw a piece of a quad near the camera (see draw_quad), ex by ez cells */\n'
    text=replace_once(text,anchor,HELPER+anchor,recipe)
    anchor='static void draw_quad_split(int x,int y,int z,int dir,int ex,int ez,int tex,int light)\n{\n'
    text=replace_once(text,anchor,anchor+'''\t/* High, cutouts and occlusion retain the original adaptive decisions. */
\tif(g_renderScale==2 && !occCapture && !occQuery && !texTransparent[tex] &&
\t   (ex>1 || ez>1) && ex<=2 && ez<=2 && split_depth_safe(x,y,z,dir,ex,ez))
\t{
\t\tdraw_quad(x,y,z,dir|8,ex,ez,tex,light);
\t\treturn;
\t}
''',recipe)
    return text.encode('utf-8'),recipe
