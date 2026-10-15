"""N64 source adaptations shared by stage_core.py and stage_doscraft.py.

Each is an exact, fail-closed replacement that returns a recipe entry, like
DOScraft's overlays. Only x86-specific code is replaced; behavior is kept.
"""
import hashlib

# Towns fmath.h: the three 64-bit-intermediate helpers are x86 inline
# assembly (imull/shrdl/idivl). The C versions give the same results for
# shifts 1..31 (the game's range): low 32 bits of the shifted 64-bit product,
# and quotients truncated toward zero, as idivl does. The x86 versions fault
# on a quotient overflow; callers guarantee there is none.
FMATH_ASM_START = '/* (a*b)>>s with a 64-bit intermediate */\n'
FMATH_ASM_END = '\n#endif\n'
FMATH_C = '''/* (a*b)>>s with a 64-bit intermediate.  N64: C in place of x86 imull/shrdl. */
static inline int mulshift(int a,int b,int s)
{
	return (int)(u32)((long long)a*b>>s);
}
/* (a<<s)/b with a 64-bit intermediate.  Caller guarantees no overflow. */
static inline int divshift(int a,int b,int s)
{
	return (int)(((long long)a<<s)/b);
}

/* (a*b)/c with a 64-bit intermediate */
static inline int muldiv(int a,int b,int c)
{
	return (int)((long long)a*b/c);
}
'''


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fmath_portable(text):
    """Replace the x86 fixed-point helpers in fmath.h with portable C."""
    if text.count(FMATH_ASM_START) != 1 or text.count(FMATH_ASM_END) != 1:
        raise ValueError('fmath.h helper markers changed')
    start, end = text.index(FMATH_ASM_START), text.index(FMATH_ASM_END)
    removed = text[start:end]
    if '__asm__' not in removed:
        raise ValueError('fmath.h helpers are no longer assembly')
    step = dict(operation='replace', file='fmath.h', before_sha256=sha256(removed.encode()),
                before_bytes=len(removed.encode()), after=FMATH_C)
    return text[:start] + FMATH_C + text[end:], step


def libc_portable(path):
    """Remove DOScraft-staged libc.c's x86 string functions (newlib has them)."""
    text = path.read_text(encoding='utf-8')
    start = 'void *memset(void *d,int c,size_t n)\n'
    end = 'char *itoa_dec(int v,char *buf)\n'
    if text.count(start) != 1 or text.count(end) != 1 or text.index(start) > text.index(end):
        raise ValueError('libc.c span markers changed')
    removed = text[text.index(start):text.index(end)]
    path.write_text(text.replace(removed, ''), encoding='utf-8')
    return dict(operation='remove', file='libc.c', removed_sha256=sha256(removed.encode()),
                removed_bytes=len(removed.encode()))


def adapt_staged(src):
    """Apply every N64 adaptation to a DOScraft-staged src/ directory."""
    steps = [libc_portable(src / 'libc.c')]
    fmath = src / 'fmath.h'
    text, step = fmath_portable(fmath.read_text(encoding='utf-8'))
    fmath.write_text(text, encoding='utf-8')
    steps.append(step)
    return steps
