"""Inline stores for short flat spans instead of out-of-line memset calls.

Planar Low/Medium fills each flat row with two memset calls of usually 1-3
bytes (-fno-builtin keeps memset a libc call, which then runs an empty
REP STOSL). Short spans now use byte stores; longer ones use the aligned
flat_span word fill. Every destination byte receives the same color.
"""

from build_imported import replace_once, lf_text

FILL = '''\twhile(tail--) *dst++=(u8)color;
}

/* Flat rows are mostly a few bytes: store those directly, words otherwise. */
static inline void flat_fill(u8 *dst,int n,u32 color)
{
\tif(n>8) flat_span(dst,n,color);
\telse while(n-->0) *dst++=(u8)color;
}
'''


def adapt(name, data, *, low_planar=False):
    if name != 'raster.c':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text, '\twhile(tail--) *dst++=(u8)color;\n}\n', FILL, recipe)
    if low_planar:
        text = replace_once(text,
            '\tmemset(row+a,color,((xr+1)>>1)-a);\n\tmemset(row+PLANAR_C+c,color,(xr>>1)-c);\n',
            '\tu32 word=(u32)color*0x01010101u;\n'
            '\tflat_fill(row+a,((xr+1)>>1)-a,word);\n\tflat_fill(row+PLANAR_C+c,(xr>>1)-c,word);\n',
            recipe)
    text = replace_once(text, 'memset(row+xl*rscale,flat,(xr-xl)*rscale);\n',
        'flat_fill(row+xl*rscale,(xr-xl)*rscale,flatWord);\n', recipe)
    return text.encode('utf-8'), recipe
