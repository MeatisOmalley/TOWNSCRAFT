"""80x50: omit alternate Very Low polygon rows, then repeat the owner.

Projection/FOV stay unchanged; expand sharp backgrounds before UI draws. Bit 1 in the existing
Very Low row mask marks discarded logical rows; no second row-mask allocation.
Normal resolutions retain their existing texel loops and finish behavior.
Applied after the exact hot-path overlays, only with Very Low texels enabled.
"""
from build_imported import replace_once,lf_text


def adapt(name,data):
    if name not in ('raster.c','trap.S'): return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    if name=='trap.S':
        text=replace_once(text,'10:\t/* ---- per row ---- */\n\tmovl\ttrapPtr,%ebp\n',
            '10:\t/* ---- per row ---- */\n\tmovl\ttrapPtr,%ebp\n'
            '\t.if \\VL\n\tmovl\tT_ROW_INDEX(%ebp),%eax\n'
            '\ttestb\t$2,_dos_very_low_rows(%eax)\n\tjnz\t80f\n\t.endif\n',recipe)
    else:
        text=replace_once(text,'#include "bench.h"\n',
            '#include "bench.h"\nextern int dos_potato_mode;\n'
            'extern u8 dos_very_low_rows[VIEW_H/2];\n',recipe)
        text=replace_once(text,
            '    trapTable=skip && dos_very_low_mode ? trapFuncVeryLow : trapFunc;\n',
            '    trapTable=(skip && dos_very_low_mode) || dos_potato_mode ? trapFuncVeryLow : trapFunc;\n',recipe)
        text=replace_once(text,
            '    memset(dos_very_low_rows,0,sizeof(dos_very_low_rows));\n',
            '    memset(dos_very_low_rows,0,sizeof(dos_very_low_rows));\n'
            '    if(dos_potato_mode)\n'
            '        for(y=0; y<VIEW_H/2; ++y) dos_very_low_rows[y]=1|((y&1)<<1);\n',recipe)
        text=replace_once(text,'    if(dos_very_low_mode)\n',
            '    if(dos_very_low_mode && !dos_potato_mode)\n',recipe)
        # Flat spans still advance both edge states on omitted rows.
        for indent in ('\t\t\t\t\t\t','\t\t\t\t\t'):
            text=replace_once(text,indent+'if(xr>xl)\n',
                indent+'if(xr>xl && (rscale!=2 || !(dos_very_low_rows[s]&2)))\n',recipe)
        text=replace_once(text,
            '    if(2==rscale)\n        for(y=0; y<rh; ++y)\n'
            '            if(!oddSkip || oddCopy[y])\n'
            '                memcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);\n',
            '    if(2==rscale)\n        for(y=0; y<rh; ++y)\n'
            '        {\n'
            '            if(dos_very_low_rows[y]&2)\n'
            '                memcpy(rbuf+y*2*rpitch,rbuf+(y-1)*2*rpitch,rw*2);\n'
            '            if(!oddSkip || oddCopy[y])\n'
            '            {\n'
            '                if(dos_potato_mode)\n'
            '                    for(int x=0;x<rw*2;x+=4)\n'
            '                        *(u32 *)(rbuf+y*2*rpitch+x)=(u32)rbuf[y*2*rpitch+x]*0x01010101u;\n'
            '                memcpy(rbuf+(y*2+1)*rpitch,rbuf+y*2*rpitch,rw*2);\n'
            '            }\n'
            '        }\n',recipe)
    return text.encode('utf-8'),recipe
