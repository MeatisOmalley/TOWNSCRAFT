#include "vga_pack.h"

int dos_vga_pack_plane(const unsigned char *source, unsigned int pitch,
                       unsigned char *destination, unsigned int capacity,
                       unsigned int plane)
{
    unsigned int x, y;
    if (!source || !destination || pitch < DOS_VGA_WIDTH ||
        capacity < DOS_VGA_PLANE_BYTES || plane >= 4) return -1;
    for (y = 0; y < DOS_VGA_HEIGHT; ++y)
        for (x = 0; x < DOS_VGA_WIDTH / 4; ++x)
            destination[y * (DOS_VGA_WIDTH / 4) + x] = source[y * pitch + x * 4 + plane];
    return 0;
}
