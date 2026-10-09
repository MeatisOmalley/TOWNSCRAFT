#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vga_pack.h"

static unsigned char source[512 * DOS_VGA_HEIGHT];
static unsigned char output[DOS_VGA_PLANE_BYTES + 8];
static unsigned char restored[512 * DOS_VGA_HEIGHT];
int main(void)
{
    unsigned int x, y, plane;
    memset(source, 0xa5, sizeof(source));
    memset(restored, 0xa5, sizeof(restored));
    for (y = 0; y < DOS_VGA_HEIGHT; ++y)
        for (x = 0; x < DOS_VGA_WIDTH; ++x)
            source[y * 512 + x] = (unsigned char)((x * 17 + y * 31) ^ (x >> 3));
    for (plane = 0; plane < 4; ++plane) {
        memset(output, 0xcd, sizeof(output));
        assert(!dos_vga_pack_plane(source, 512, output, DOS_VGA_PLANE_BYTES, plane));
        for (y = 0; y < DOS_VGA_HEIGHT; ++y)
            for (x = 0; x < DOS_VGA_WIDTH / 4; ++x)
                restored[y * 512 + x * 4 + plane] = output[y * (DOS_VGA_WIDTH / 4) + x];
        for (x = DOS_VGA_PLANE_BYTES; x < sizeof(output); ++x) assert(output[x] == 0xcd);
    }
    assert(!memcmp(source, restored, sizeof(source)));
    memset(output, 0xcd, sizeof(output));
    assert(dos_vga_pack_plane(source, 319, output, sizeof(output), 0) == -1);
    assert(dos_vga_pack_plane(source, 512, output, DOS_VGA_PLANE_BYTES - 1, 0) == -1);
    assert(dos_vga_pack_plane(source, 512, output, sizeof(output), 4) == -1);
    assert(dos_vga_pack_plane(0, 512, output, sizeof(output), 0) == -1);
    assert(dos_vga_pack_plane(source, 512, 0, sizeof(output), 0) == -1);
    for (x = 0; x < sizeof(output); ++x) assert(output[x] == 0xcd);
    puts("PASS: all 76800 indexed pixels, padding, write bounds, invalid input");
    return 0;
}
