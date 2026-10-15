#include <stddef.h>
#include "vga_pack.h"

/* Experimental transport helper, not selected by the production presenter.
 * Source and destination must not overlap. The caller supplies 320 readable
 * bytes per source row and four consecutive 19,200-byte destination planes.
 * Byte accesses support unaligned buffers without aliasing assumptions.
 */
int dos_vga_pack_all(const unsigned char *source, unsigned int pitch,
                     unsigned char *destination, unsigned int capacity)
{
    unsigned int x, y;
    unsigned char *p0, *p1, *p2, *p3;
    if (!source || !destination || pitch < DOS_VGA_WIDTH ||
        capacity < 4u * DOS_VGA_PLANE_BYTES) return -1;

    p0 = destination;
    p1 = p0 + DOS_VGA_PLANE_BYTES;
    p2 = p1 + DOS_VGA_PLANE_BYTES;
    p3 = p2 + DOS_VGA_PLANE_BYTES;
    for (y = 0; y < DOS_VGA_HEIGHT; ++y) {
        const unsigned char *row = source + (size_t)y * pitch;
        /* Four adjacent pixel groups per iteration; 80 groups divide evenly.
         * This limits loop overhead without wide/unaligned source loads.
         * Any target speedup still requires guest measurement.
         */
#define PACK_GROUP(i) do { \
            p0[i] = row[4u * (i)]; \
            p1[i] = row[4u * (i) + 1u]; \
            p2[i] = row[4u * (i) + 2u]; \
            p3[i] = row[4u * (i) + 3u]; \
        } while (0)
        for (x = 0; x < DOS_VGA_WIDTH / 4; x += 4) {
            PACK_GROUP(0);
            PACK_GROUP(1);
            PACK_GROUP(2);
            PACK_GROUP(3);
            row += 16;
            p0 += 4;
            p1 += 4;
            p2 += 4;
            p3 += 4;
        }
#undef PACK_GROUP
    }
    return 0;
}
