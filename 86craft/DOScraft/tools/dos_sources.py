"""Explicit DOS-only source overlays; the pinned Towns snapshot stays immutable.

Every change is a replayable, fail-closed replacement in the build manifest.
Experiments remain independently selectable; the direct-port builder defaults
to no overlays. No automatic copying from a different local Towns branch.
"""

from build_imported import replace_once


def adapt(name, data, *, graphics=False, raster_zero_dx=False):
    recipe=[]
    if graphics:
        import dos_graphics
        data, steps=dos_graphics.adapt(name,data)
        recipe.extend(steps)
    if name=='raster.c' and raster_zero_dx:
        text=data.decode('utf-8')
        if '\r\n' in text:
            recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
            text=text.replace('\r\n','\n')
        text=replace_once(text,
            '\te->dx=divshift(b->x-a->x,dy,16);\n',
            '\t/* Accepted polygon edges have dy>0. Exact vertical edges need no IDIV. */\n'
            '\te->dx=(b->x==a->x) ? 0 : divshift(b->x-a->x,dy,16);\n',recipe)
        data=text.encode('utf-8')
    return data,recipe
