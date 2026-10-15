#include <limits.h>
#include <string.h>
#include "vga_hud_cache.h"

/* GNU/Clang explicitly permit aliasing byte storage and unaligned word loads.
 * Other compilers (or unusual integer widths) use the byte-only path. The
 * portable override lets native tests exercise that path on GNU compilers.
 */
#if (defined(__GNUC__) || defined(__clang__)) && UINT_MAX == 0xffffffffu && \
    CHAR_BIT == 8 && !defined(DOS_VGA_HUD_CACHE_PORTABLE)
typedef unsigned int HudWord __attribute__((__may_alias__, __aligned__(1)));
#define HUD_WORD_COMPARE 1
#endif

int dos_vga_hud_changed(const unsigned char *newhud, unsigned char *shadow,
                         unsigned int bytes)
{
    unsigned int i = 0;
    if (!newhud || !shadow || bytes > DOS_VGA_HUD_CACHE_MAX_BYTES) return -1;
#ifdef HUD_WORD_COMPARE
    for (; bytes - i >= 4u; i += 4u)
        if (*(const HudWord *)(newhud + i) != *(const HudWord *)(shadow + i))
            goto changed;
#endif
    for (; i < bytes; ++i)
        if (newhud[i] != shadow[i]) goto changed;
    return 0;

changed:
    /* The DOS runtime already supplies rep-movsl/movsb memcpy. */
    memcpy(shadow, newhud, bytes);
    return 1;
}
