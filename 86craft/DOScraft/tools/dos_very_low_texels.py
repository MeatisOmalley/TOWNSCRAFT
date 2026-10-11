"""Draw only the texels sampled by the Very Low VGA path on proven rows."""

from build_imported import replace_once


def adapt(name, data):
    if name not in ('raster.c', 'trap.S'):
        return data, []
    recipe=[]
    text=data.decode('utf-8')
    if '\r\n' in text:
        recipe.append(dict(operation='replace',before='\r\n',after='\n',count=text.count('\r\n')))
        text=text.replace('\r\n','\n')
    if name=='raster.c':
        text=replace_once(text,'\tu32 pixels;\n} Trap;\n',
            '\tu32 pixels;\n\tint rowIndex;\n} Trap;\n',recipe)
        text=replace_once(text,'static int oddSkip;\n',
            'static int oddSkip;\nextern int dos_very_low_mode;\n'
            'u8 dos_very_low_rows[VIEW_H/2];\n',recipe)
        text=replace_once(text,'    oddSkip=skip;\n    if(!skip) return;\n',
            '    int y;\n    oddSkip=skip;\n    memset(dos_very_low_rows,0,sizeof(dos_very_low_rows));\n'
            '    if(!skip) return;\n',recipe)
        text=replace_once(text,'    if(debug) odd_copy_range(0,96);\n',
            '    if(debug) odd_copy_range(0,96);\n'
            '    if(dos_very_low_mode)\n'
            '        for(y=0; y<VIEW_H/2; ++y) dos_very_low_rows[y]=!oddCopy[y];\n',recipe)
        text=replace_once(text,'\t\t\t\tt.rows=end-s;\n',
            '\t\t\t\tt.rows=end-s;\n\t\t\t\tt.rowIndex=s;\n',recipe)
    else:
        text=replace_once(text,'\t.set T_PIXELS,68\n',
            '\t.set T_PIXELS,68\n\t.set T_ROW_INDEX,72\n',recipe)
        text=replace_once(text,'\t/* SCALE: 1 or 2 bytes per pixel.  TRANSP: skip texel 0.  WRAP: see above. */\n',
            '''\t/* Advance UV without fetching or storing a discarded texel. */
\t.macro SKIPTEXEL WRAP
\t.if \\WRAP
\taddl\tduFracT,%ecx
\t.else
\taddl\t%esi,%ecx
\t.endif
\tadcb\t%dl,%bl
\taddw\t%bp,%cx
\tadcb\t%dh,%bh
\t.if \\WRAP
\tandl\t$0x0F0F,%ebx
\t.endif
\t.endm

\t/* SCALE: 1 or 2 bytes per pixel.  TRANSP: skip texel 0.  WRAP: see above. */
''',recipe)
        text=replace_once(text,'\t.if !\\TRANSP\n',
            '''\t.if \\SCALE==2
\tmovl\ttrapPtr,%eax
\tmovl\tT_ROW_INDEX(%eax),%eax
\tcmpb\t$0,_dos_very_low_rows(%eax)
\tjne\t30f
\t.endif
\t.if !\\TRANSP
''',recipe)
        text=replace_once(text,'\t.endif\n\n80:',
            '''\t.endif
\tjmp\t80f

30:\t/* Very Low: preserve even logical texels; VGA repeats each four pixels. */
\ttestb\t$1,rowX
\tjz\t31f
\tSKIPTEXEL \\WRAP
\taddl\t$2,%edi
\tdecl\trowW
31:
\tmovl\trowW,%eax
\tshrl\t$1,%eax
\tjz\t33f
\tdecl\t%eax
\tshll\t$16,%eax
32:
\tTEXEL\t2,\\TRANSP,\\WRAP,0
\tSKIPTEXEL \\WRAP
\taddl\t$4,%edi
\tsubl\t$0x10000,%eax
\tjnc\t32b
33:
\ttestb\t$1,rowW
\tjz\t80f
\tTEXEL\t2,\\TRANSP,\\WRAP,0

80:''',recipe)
        text=replace_once(text,'\taddl\t%eax,T_ROW(%ebp)\n',
            '\taddl\t%eax,T_ROW(%ebp)\n\t.if \\SCALE==2\n'
            '\tincl\tT_ROW_INDEX(%ebp)\n\t.endif\n',recipe)
    return text.encode('utf-8'),recipe
