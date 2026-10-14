"""Overlapping upward memmove: copy dwords from the top down.

The vendor memmove copied an overlapping region with dp>sp one byte per
loop iteration. pool_compact() moves mesh ranges this way. Copy the 0-3
trailing bytes and then whole dwords with the direction flag set, like
DJGPP's own memmove; the C ISRs already CLD on entry. Reading each dword
before storing it, from high to low addresses, is exact for any overlap.
"""
from build_imported import replace_once


def adapt(name, data):
    if name != 'libc.c':
        return data, []
    text = data.decode('utf-8')
    recipe = []
    if '\r\n' in text:
        recipe.append(dict(operation='replace', before='\r\n', after='\n', count=text.count('\r\n')))
        text = text.replace('\r\n', '\n')
    text = replace_once(text,
        '\twhile(n--)\n'
        '\t{\n'
        '\t\tdp[n]=sp[n];\n'
        '\t}\n'
        '\treturn d;\n',
        '\t{\n'
        '\t\t/* Top-down: trailing bytes, then dwords; restore DF for C. */\n'
        '\t\tunsigned int nb=n&3,nd=n>>2;\n'
        '\t\tu8 *de=dp+n-1;\n'
        '\t\tconst u8 *se=sp+n-1;\n'
        '\t\t__asm__ volatile("std\\n\\trep movsb\\n\\tsubl $3,%%edi\\n\\tsubl $3,%%esi\\n\\t"\n'
        '\t\t\t"movl %3,%%ecx\\n\\trep movsl\\n\\tcld"\n'
        '\t\t\t:"+D"(de),"+S"(se),"+c"(nb):"r"(nd):"memory","cc");\n'
        '\t}\n'
        '\treturn d;\n', recipe)
    return text.encode('utf-8'), recipe
