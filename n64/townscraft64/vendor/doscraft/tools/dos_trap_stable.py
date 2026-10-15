"""Exact constant-gradient opaque scale-2 specialization, independently selectable."""
from build_imported import replace_once,lf_text


def adapt(name,data):
    if name!='trap.S': return data,[]
    recipe=[]
    text=lf_text(data,recipe)
    start=text.index('\t/* Span width in pixels for the gradient */\n')
    end=text.index('\tmovl\t%eax,rowDV\n',start)+len('\tmovl\t%eax,rowDV\n')
    gradient=text[start:end]
    text=replace_once(text,gradient,'\t.if !\\CACHED\n\tGRADIENT\n\t.endif\n',recipe)
    anchor='\t/* SCALE: 1 or 2 bytes per pixel.  TRANSP: skip texel 0.  WRAP: see above. */\n'
    text=replace_once(text,anchor,'\t.macro GRADIENT\n'+gradient+'\t.endm\n\n'+anchor,recipe)
    text=replace_once(text,'\t.macro TRAP name,SCALE,TRANSP,WRAP\n',
        '\t.macro TRAP name,SCALE,TRANSP,WRAP,CACHED=0\n',recipe)
    text=replace_once(text,'\tjle\t99f\n\n10:',
        '\tjle\t99f\n\t.if \\CACHED\n\tmovl\t%eax,%ebp\n\tGRADIENT\n\t.endif\n\n10:',recipe)
    for wrap in (False,True):
        symbol='trap_opaque2'+('w' if wrap else '')
        prefix='_'+symbol if '\tTRAP _'+symbol+',' in text else symbol
        before=f'\tTRAP {prefix},2,0,{int(wrap)}\n'
        original=prefix+'_original'; cached=prefix+'_stable'
        after=f'''\t/* Pay eligibility once, never a cache lookup in the row loop.
\t   Equal edge increments preserve raw width and both UV deltas exactly. */
\t.globl {prefix}
{prefix}:
\tmovl 4(%esp),%eax
\tcmpl $4,T_ROWS(%eax)
\tjl {original}
\tmovl T_LDX(%eax),%edx
\tcmpl T_RDX(%eax),%edx
\tjne {original}
\tmovl T_LDU(%eax),%edx
\tcmpl T_RDU(%eax),%edx
\tjne {original}
\tmovl T_LDV(%eax),%edx
\tcmpl T_RDV(%eax),%edx
\tjne {original}
\tjmp {cached}
\tTRAP {original},2,0,{int(wrap)},0
\tTRAP {cached},2,0,{int(wrap)},1
'''
        text=replace_once(text,before,after,recipe)
    return text.encode('utf-8'),recipe
