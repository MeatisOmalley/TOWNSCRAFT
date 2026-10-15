"""Row gradients with one-operand MUL instead of two-operand IMUL.

Only the low 32 bits reach `sarl $6`, and they are identical for signed and
unsigned products. EDX is dead there (reloaded with T_LV below). 86Box charges
30 clocks for IMUL r32,r/m32 and 21 for MUL r/m32; a real i486 charges both
13-42 by the same early-out rule, so this is an emulator-only saving.
src/platform/dos/low_planar.S makes the same change under DOS_ROW_MUL.
"""

from build_imported import replace_once, lf_text


def adapt(name, data):
    if name != 'trap.S':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    for field in ('DU', 'DV'):
        text = replace_once(text,
            '\tsarl\t$8,%eax\n\timull\t%ecx,%eax\n\tsarl\t$6,%eax\n\tmovl\t%eax,row' + field + '\n',
            '\tsarl\t$8,%eax\n\tmull\t%ecx\t\t\t/* Same low 32 bits; EDX is dead */\n'
            '\tsarl\t$6,%eax\n\tmovl\t%eax,row' + field + '\n', recipe)
    return text.encode('utf-8'), recipe
