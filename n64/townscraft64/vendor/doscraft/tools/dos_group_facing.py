"""Move the existing sorted-prefix backface rejection before group box work."""
from build_imported import replace_once,lf_text

def adapt(name,data):
    if name!='render.c': return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    before='''\t\t\t\t\t/* The group's box: distance limit, then the planes the
\t\t\t\t\t   chunk straddles */
'''
    after='''\t\t\t\t\t/* Published group planes are sorted facing-first. The original
\t\t\t\t\t   inner loop stops at the first backface; fail that same test
\t\t\t\t\t   here before paying group bounds/distance/frustum setup. */
\t\t\t\t\t{
\t\t\t\t\t\tu32 first=q[0];
\t\t\t\t\t\tint x=x0+MQ_LX(first),y=y0+MQ_LY(first),z=z0+MQ_LZ(first);
\t\t\t\t\t\tif(0==((visX[x]|visY[y]|visZ[z])&(1<<dir))) {
\t\t\t\t\t\t\tq=gend; continue;
\t\t\t\t\t\t}
\t\t\t\t\t}
'''+before
    text=replace_once(text,before,after,recipe)
    return text.encode('utf-8'),recipe
