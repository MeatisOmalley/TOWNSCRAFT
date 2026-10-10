#include <stddef.h>
#include <stdint.h>
#include "vga_pack_words.h"

#if defined(__GNUC__) && !defined(DOS_VGA_PACK_WORDS_PORTABLE) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
/* GNU explicitly permits aliasing any object and alignment of one byte.
 * i486 supports unaligned dword accesses (with a hardware timing penalty).
 */
typedef uint32_t pack_word __attribute__((__may_alias__, __aligned__(1)));
static inline uint32_t load_word(const unsigned char *p)
{
    return *(const pack_word *)p;
}
static inline void store_word(unsigned char *p, uint32_t value)
{
    *(pack_word *)p = value;
}
#else
/* Byte-only, endian-independent fallback; no memcpy/library dependency. */
static inline uint32_t load_word(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void store_word(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}
#endif

int dos_vga_pack_words(const unsigned char *source, unsigned int pitch,
                       unsigned char *destination, unsigned int capacity)
{
    unsigned int y;
    if (!source || !destination || pitch < DOS_VGA_WIDTH ||
        capacity < 4u * DOS_VGA_PLANE_BYTES) return -1;

    for (y = 0; y < DOS_VGA_HEIGHT; ++y) {
        const unsigned char *row = source + (size_t)y * pitch;
        unsigned char *out = destination + y * (DOS_VGA_WIDTH / 4u);
        unsigned char *end = out + DOS_VGA_WIDTH / 4u;
        do {
            uint32_t a = load_word(row), b = load_word(row + 4);
            uint32_t c = load_word(row + 8), d = load_word(row + 12);
#if defined(__GNUC__) && defined(__i386__) && \
    !defined(DOS_VGA_PACK_WORDS_PORTABLE)
            /* All four byte-addressable i486 registers hold one source word.
             * xchg ah,bl swaps a1/b0; rotations expose a3/b2 for the same
             * exchange, then restore byte order. Repeat for c/d. Finally
             * rotate/exchange/rotate swaps a's high half with c's low half
             * (and b/d), leaving exactly the four plane words described below.
             * Register operands only: no hidden memory access or alias rule.
             * 12 immediate rotates + 6 register exchanges; no newer ISA.
             */
            __asm__ (
                "xchgb %%ah, %%bl\n\t"
                "rorl $16, %%eax\n\t"
                "rorl $16, %%ebx\n\t"
                "xchgb %%ah, %%bl\n\t"
                "rorl $16, %%eax\n\t"
                "rorl $16, %%ebx\n\t"
                "xchgb %%ch, %%dl\n\t"
                "rorl $16, %%ecx\n\t"
                "rorl $16, %%edx\n\t"
                "xchgb %%ch, %%dl\n\t"
                "rorl $16, %%ecx\n\t"
                "rorl $16, %%edx\n\t"
                "rorl $16, %%eax\n\t"
                "xchgw %%ax, %%cx\n\t"
                "rorl $16, %%eax\n\t"
                "rorl $16, %%ebx\n\t"
                "xchgw %%bx, %%dx\n\t"
                "rorl $16, %%ebx"
                : "+a" (a), "+b" (b), "+c" (c), "+d" (d)
                : : "cc");
#else
            uint32_t t;
            /* Bytes below are low to high. The first stage changes
             * [a0 a1 a2 a3], [b0 b1 b2 b3] into
             * [a0 b0 a2 b2], [a1 b1 a3 b3], likewise for c/d.
             * Each XOR swap touches only the selected disjoint bytes.
             */
            t = ((a >> 8) ^ b) & UINT32_C(0x00ff00ff);
            b ^= t;
            a ^= t << 8;
            t = ((c >> 8) ^ d) & UINT32_C(0x00ff00ff);
            d ^= t;
            c ^= t << 8;
            /* Swap high/low halfwords to obtain one word per plane:
             * a=[a0 b0 c0 d0], b=[a1 b1 c1 d1],
             * c=[a2 b2 c2 d2], d=[a3 b3 c3 d3].
             */
            t = ((a >> 16) ^ c) & UINT32_C(0x0000ffff);
            c ^= t;
            a ^= t << 16;
            t = ((b >> 16) ^ d) & UINT32_C(0x0000ffff);
            d ^= t;
            b ^= t << 16;
#endif
            store_word(out, a);
            store_word(out + DOS_VGA_PLANE_BYTES, b);
            store_word(out + 2u * DOS_VGA_PLANE_BYTES, c);
            store_word(out + 3u * DOS_VGA_PLANE_BYTES, d);
            row += 16;
            out += 4;
        } while (out != end);
    }
    return 0;
}
