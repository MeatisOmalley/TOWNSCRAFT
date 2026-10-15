#ifndef DOSCRAFT_VGA_PACK_WORDS_H
#define DOSCRAFT_VGA_PACK_WORDS_H

#include "vga_pack.h"

/* Independent experiment: explicitly link vga_pack_words.c and call this
 * symbol to select it. No presenter/build selection is changed.
 * Same contract as dos_vga_pack_all: 320 readable bytes in each of 240 rows,
 * pitch >= 320, four consecutive 19,200-byte output planes, no overlap.
 * Unaligned buffers are permitted. Returns 0 on success, -1 for a null
 * pointer, short pitch or short capacity, before reading/writing buffers.
 * GNU i386 uses an i486 register transpose; other targets use C XOR swaps.
 * Define DOS_VGA_PACK_WORDS_PORTABLE to force byte-only C word accesses.
 * DOS_VGA_PACK_WORDS_C_TRANSPOSE retains word accesses but forces XOR swaps.
 * Performance/adoption requires guest timing; native parity is not a speed test.
 * Source allocation must cover (239 * pitch + 320) bytes; like the original,
 * this interface cannot check allocation size or pointer arithmetic overflow.
 */
int dos_vga_pack_words(const unsigned char *source, unsigned int pitch,
                       unsigned char *destination, unsigned int capacity);

/* Separate row-packing probe; disabled in production. */
/* Exact 4x4 byte transpose. Unaligned alias-safe words are supported on i386. */
typedef unsigned int DosPackWord __attribute__((__may_alias__,__aligned__(1)));
static inline void dos_vga_pack_row_words(const unsigned char *src,
    unsigned char *a,unsigned char *b,unsigned char *c,unsigned char *d)
{
    for(int x=0;x<80;x+=4,src+=16) {
        unsigned u0=*(const DosPackWord *)(src),u1=*(const DosPackWord *)(src+4);
        unsigned u2=*(const DosPackWord *)(src+8),u3=*(const DosPackWord *)(src+12);
        unsigned t0=(u0&0x00ff00ffu)|((u1&0x00ff00ffu)<<8);
        unsigned t1=((u0>>8)&0x00ff00ffu)|(u1&0xff00ff00u);
        unsigned t2=(u2&0x00ff00ffu)|((u3&0x00ff00ffu)<<8);
        unsigned t3=((u2>>8)&0x00ff00ffu)|(u3&0xff00ff00u);
        *(DosPackWord *)(a+x)=(t0&0xffffu)|(t2<<16);
        *(DosPackWord *)(b+x)=(t1&0xffffu)|(t3<<16);
        *(DosPackWord *)(c+x)=(t0>>16)|(t2&0xffff0000u);
        *(DosPackWord *)(d+x)=(t1>>16)|(t3&0xffff0000u);
    }
}

#endif
