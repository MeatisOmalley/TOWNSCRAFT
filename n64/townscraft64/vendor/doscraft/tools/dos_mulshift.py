"""mulshift via unsigned MUL plus an exact signed high-word correction.

The signed 64-bit product's high word is the unsigned one minus b when a<0
and minus a when b<0 (mod 2^32); the low word is identical. 86Box charges
38 clocks for one-operand IMUL and 21 for MUL. A real i486 charges both
13-42 by the same rule, so there the branch-free correction is pure overhead.
Rejected 2026-10-13: exact, but register spills made Low 0.04-0.28 ms slower
even in 86Box. Kept selectable only.
"""

from build_imported import replace_once, lf_text

BEFORE = '''\tint lo,hi;
\tif(__builtin_constant_p(s))
\t{
\t\t__asm__("imull %3\\n\\tshrdl %4,%%edx,%%eax":"=a"(lo),"=d"(hi):"a"(a),"rm"(b),"I"(s):"cc");
\t\treturn lo;
\t}
\t__asm__("imull %3":"=a"(lo),"=d"(hi):"a"(a),"rm"(b));
\treturn (int)(((u32)lo>>s)|((u32)hi<<(32-s)));
'''

AFTER = '''\tu32 lo,hi;
\t__asm__("mull %3":"=a"(lo),"=d"(hi):"a"(a),"rm"(b):"cc");
\thi-=(u32)((a>>31)&b)+(u32)((b>>31)&a);   /* Signed high word */
\tif(__builtin_constant_p(s))
\t{
\t\t__asm__("shrdl %2,%1,%0":"+r"(lo):"r"(hi),"I"(s):"cc");
\t\treturn (int)lo;
\t}
\treturn (int)((lo>>s)|(hi<<(32-s)));
'''


def adapt(name, data):
    if name != 'fmath.h':
        return data, []
    recipe = []
    text = lf_text(data, recipe)
    text = replace_once(text, BEFORE, AFTER, recipe)
    return text.encode('utf-8'), recipe
