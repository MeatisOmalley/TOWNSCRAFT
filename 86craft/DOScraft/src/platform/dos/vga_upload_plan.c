#include <limits.h>
#include <stddef.h>
#include "vga_upload_plan.h"

#if (defined(__GNUC__) || defined(__clang__)) && UINT_MAX == 0xffffffffu && \
    CHAR_BIT == 8 && !defined(DOS_VGA_UPLOAD_PLAN_PORTABLE)
typedef unsigned int UploadWord __attribute__((__may_alias__, __aligned__(1)));
#define UPLOAD_WORD_COMPARE 1
#endif

static int rows_equal(const unsigned char *a, const unsigned char *b)
{
    unsigned int i = 0;
#ifdef UPLOAD_WORD_COMPARE
    for (; i < DOS_VGA_UPLOAD_ROW_BYTES; i += 4u)
        if (*(const UploadWord *)(a + i) != *(const UploadWord *)(b + i)) return 0;
#else
    for (; i < DOS_VGA_UPLOAD_ROW_BYTES; ++i)
        if (a[i] != b[i]) return 0;
#endif
    return 1;
}

int dos_vga_upload_plan(const unsigned char *planes, unsigned int plane_stride,
                        unsigned int rows, struct DosVgaRun *runs,
                        unsigned int capacity)
{
    unsigned char masks[4][DOS_VGA_UPLOAD_MAX_ROWS];
    unsigned int row, plane, count = 0;
    if (!planes || !runs || rows > DOS_VGA_UPLOAD_MAX_ROWS ||
        plane_stride < rows * DOS_VGA_UPLOAD_ROW_BYTES || capacity < 4u * rows)
        return -1;
    if (!rows) return 0;

    for (row = 0; row < rows; ++row) {
        const unsigned char *p0 = planes + row * DOS_VGA_UPLOAD_ROW_BYTES;
        const unsigned char *p1 = p0 + (size_t)plane_stride;
        const unsigned char *p2 = p0 + (size_t)plane_stride * 2u;
        const unsigned char *p3 = p0 + (size_t)plane_stride * 3u;
        int pair01 = rows_equal(p0, p1), pair23 = rows_equal(p2, p3);
        if (pair01 && pair23 && rows_equal(p0, p2)) {
            masks[0][row] = 15;
            masks[1][row] = masks[2][row] = masks[3][row] = 0;
        } else {
            masks[0][row] = pair01 ? 3 : 1;
            masks[1][row] = pair01 ? 0 : 2;
            masks[2][row] = pair23 ? 12 : 4;
            masks[3][row] = pair23 ? 0 : 8;
        }
    }
    for (plane = 0; plane < 4; ++plane) {
        row = 0;
        while (row < rows) {
            unsigned int start = row;
            unsigned char mask = masks[plane][row++];
            if (!mask) continue;
            while (row < rows && masks[plane][row] == mask) ++row;
            runs[count].row = (unsigned short)start;
            runs[count].rows = (unsigned short)(row - start);
            runs[count].mask = mask;
            runs[count].plane = (unsigned char)plane;
            ++count;
        }
    }
    return (int)count;
}
