/* WebAssembly entry points around Nuked OPL3 for the FM instrument lab.
 *
 * Built freestanding (no libc): the few C library calls opl3.c makes are
 * provided below. One chip instance and one output buffer live in linear
 * memory; the AudioWorklet writes registers and pulls interleaved stereo
 * 16-bit frames.
 *
 * Copyright (C) 2026 the DOScraft authors. Licensed like Nuked OPL3, under the
 * GNU Lesser General Public License version 2.1 or later (see ../nuked/LICENSE).
 */
#include <stddef.h>
#include "../nuked/opl3.h"

#define EXPORT(name) __attribute__((export_name(name)))
#define MAX_FRAMES 4096

static opl3_chip chip;
static int16_t frames[MAX_FRAMES * 2];

void *memset(void *dest, int value, size_t count)
{
    unsigned char *d = dest;
    while (count--)
        *d++ = (unsigned char)value;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (count--)
        *d++ = *s++;
    return dest;
}

/* samplerate: output rate in Hz. 49716 is the chip's own rate (no resampling). */
EXPORT("opl_reset") void opl_reset(uint32_t samplerate)
{
    OPL3_Reset(&chip, samplerate);
}

/* Register write through the chip's write buffer, which spaces writes a couple
 * of chip samples apart as real port timing does (so a key-off followed at
 * once by a key-on still retriggers the envelope). reg 0x100-0x1FF is the
 * OPL3 second bank. */
EXPORT("opl_write") void opl_write(uint32_t reg, uint32_t value)
{
    OPL3_WriteRegBuffered(&chip, (uint16_t)reg, (uint8_t)value);
}

/* Render count stereo frames (count <= opl_capacity()); returns the buffer. */
EXPORT("opl_render") int16_t *opl_render(uint32_t count)
{
    if (count > MAX_FRAMES)
        count = MAX_FRAMES;
    OPL3_GenerateStream(&chip, frames, count);
    return frames;
}

EXPORT("opl_capacity") uint32_t opl_capacity(void)
{
    return MAX_FRAMES;
}
