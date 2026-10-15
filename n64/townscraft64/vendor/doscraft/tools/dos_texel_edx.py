"""Step the v fraction in the upper half of EDX instead of 16-bit CX.

The pinned texel step `addw %bp,%cx` needs an operand-size prefix on every
texel (+1 clock on an i486, +2 in 86Box). Keeping v frac<<16 in EDX and
dv frac<<16 in EBP gives the same 16-bit sum and carry with a 32-bit ADD:
the low halves of EBP are zero, so DL (du int) and DH (dv int) are unchanged.
src/platform/dos/low_planar.S makes the same change under DOS_TEXEL_EDX.
"""

from build_imported import replace_once, lf_text

SETUP_BEFORE = '''\tshll\t$16,%ecx
\tmovw\t%dx,%cx
\t.if \\WRAP
\tmovl\tT_TILE(%ebp),%esi
\t.else
\taddl\tT_TILE(%ebp),%ebx\t/* Texel address */
\t.endif

\tmovl\trowDU,%eax
\tmovl\t%eax,%edx
\tshll\t$16,%eax
\t.if \\WRAP
\tmovl\t%eax,duFracT
\t.else
\tmovl\t%eax,%esi
\t.endif
\tsarl\t$16,%edx\t\t/* DL = du int */
\tmovl\trowDV,%ebp\t\t/* BP = dv frac */
\tmovl\t%ebp,%eax
\tsarl\t$16,%eax
\tmovb\t%al,%dh\t\t\t/* DH = dv int */
'''

SETUP_AFTER = '''\t/* ECX = u frac:0, EDX = v frac:dv int:du int, EBP = dv frac:0 */
\tshll\t$16,%ecx
\tshll\t$16,%edx
\t.if \\WRAP
\tmovl\tT_TILE(%ebp),%esi
\t.else
\taddl\tT_TILE(%ebp),%ebx\t/* Texel address */
\t.endif

\tmovl\trowDU,%eax
\tshll\t$16,%eax
\t.if \\WRAP
\tmovl\t%eax,duFracT
\t.else
\tmovl\t%eax,%esi
\t.endif
\tmovb\trowDU+2,%dl\t\t/* DL = du int */
\tmovb\trowDV+2,%dh\t\t/* DH = dv int */
\tmovl\trowDV,%ebp
\tshll\t$16,%ebp\t\t/* EBP = dv frac:0 */
'''

STEP = '\tadcb\t%dl,%bl\n\taddw\t%bp,%cx\n\tadcb\t%dh,%bh\n'
STEP_EDX = '\tadcb\t%dl,%bl\n\taddl\t%ebp,%edx\n\tadcb\t%dh,%bh\n'


def adapt(name, data, *, very_low_texels=False):
    if name != 'trap.S':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text, '\tmovb\t(%ebx),%al\n\taddl\t%esi,%ecx\n\t.endif\n' + STEP,
        '\tmovb\t(%ebx),%al\n\taddl\t%esi,%ecx\n\t.endif\n' + STEP_EDX, recipe)
    if very_low_texels:
        text = replace_once(text, '\t.else\n\taddl\t%esi,%ecx\n\t.endif\n' + STEP,
            '\t.else\n\taddl\t%esi,%ecx\n\t.endif\n' + STEP_EDX, recipe)
    if '%bp,%cx' in text:
        raise ValueError('Every texel step must use the EDX v fraction')
    text = replace_once(text, SETUP_BEFORE, SETUP_AFTER, recipe)
    text = replace_once(text, '\t/* Texel index BH=v int, BL=u int; ECX = u frac<<16 | v frac */\n',
        '\t/* Texel index BH=v int, BL=u int */\n', recipe)
    text = replace_once(text, '  and BL=u (integer texels), ECX holds the fractions (u in the upper half,\n'
        '  v in the lower half), and stepping is ADD/ADC.',
        '  and BL=u (integer texels), ECX holds the u fraction in its upper half,\n'
        '  EDX the v fraction in its upper half, and stepping is ADD/ADC.', recipe)
    return text.encode('utf-8'), recipe
