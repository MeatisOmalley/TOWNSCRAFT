"""Keep the raster row stride and carry the row pointer across trapezoids.

poly_scan multiplied rpitch*rscale for every polygon and s*stride for every
trapezoid. The stride now changes only where the target does. Every trap loop
advances T_ROW by T_STRIDE once per row, including empty rows, so after a call
t.row is already the first row of the next trapezoid; the flat loops advance
the same pointer row by row.
"""

import re

from build_imported import replace_once, lf_text


def adapt(name, data, *, low_planar=False):
    if name != 'raster.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text, 'static int rw,rh,rpitch,rscale;\n',
        'static int rw,rh,rpitch,rscale;\nstatic int rstride;   /* rpitch*rscale */\n', recipe)
    text = replace_once(text, '\tif(0==g_recip14[1])\n',
        '\trstride=rpitch*rscale;\n\tif(0==g_recip14[1])\n', recipe)
    if low_planar:
        text = replace_once(text, '        rpitch=planarFbPitch;\n        return;\n',
            '        rpitch=planarFbPitch;\n        rstride=rpitch*rscale;\n        return;\n', recipe)
    text = replace_once(text, '\t\tint stride=rpitch*rscale;\n',
        '\t\tint stride=rstride;\n\t\tu8 *row=rbuf+sTop*stride;\n', recipe)
    text = replace_once(text, '\t\t\t\tt.row=rbuf+s*stride;\n', '\t\t\t\tt.row=row;\n', recipe)
    text = replace_once(text, '\t\t\t\ttrap(&t);\n', '\t\t\t\ttrap(&t);\n\t\t\t\trow=t.row;\n', recipe)
    text = replace_once(text, '\t\t\t\tu8 *row=rbuf+s*stride;\n', '', recipe)
    # Fail closed if another overlay changes the target somewhere new.
    if len(re.findall(r'\b(?:rpitch|rscale)=(?!=)', text)) != (4 if low_planar else 2):
        raise ValueError('Every raster target change must refresh rstride')
    return text.encode('utf-8'), recipe
