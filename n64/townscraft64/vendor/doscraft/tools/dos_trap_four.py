"""Independent four-texel opaque loop; original transparent loops retained."""
from build_imported import replace_once,lf_text

FOUR='''\t/* Four opaque texels per iteration. The first partial group enters
\t   at its first required store; no extra texture reads/writes. */
\tmovl\trowW,%eax
\taddl\t$3,%eax
\tshrl\t$2,%eax
\tdecl\t%eax
\tshll\t$16,%eax
\ttestb\t$3,rowW
\tjz\t20f
\ttestb\t$2,rowW
\tjnz\t17f
\tsubl\t$3*\\SCALE,%edi
\tjmp\t23f
17:
\ttestb\t$1,rowW
\tjnz\t18f
\tsubl\t$2*\\SCALE,%edi
\tjmp\t22f
18:
\tsubl\t$\\SCALE,%edi
\tjmp\t21f
20:
\tTEXEL\t\\SCALE,\\TRANSP,\\WRAP,0
21:
\tTEXEL\t\\SCALE,\\TRANSP,\\WRAP,\\SCALE
22:
\tTEXEL\t\\SCALE,\\TRANSP,\\WRAP,2*\\SCALE
23:
\tTEXEL\t\\SCALE,\\TRANSP,\\WRAP,3*\\SCALE
\taddl\t$4*\\SCALE,%edi
\tsubl\t$0x10000,%eax
\tjnc\t20b
'''


def adapt(name,data):
    if name!='trap.S': return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    start=text.index('\t/* Pixel pairs; an odd count enters at the second pixel of a pair */\n')
    end=text.index('\n80:',start)
    before=text[start:end]
    text=replace_once(text,before,'\t.if !\\TRANSP\n'+FOUR+'\t.else\n'+before+'\t.endif\n',recipe)
    return text.encode('utf-8'),recipe
