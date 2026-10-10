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

#endif
