#ifndef DOSCRAFT_VGA_UPLOAD_PLAN_H
#define DOSCRAFT_VGA_UPLOAD_PLAN_H

#define DOS_VGA_UPLOAD_ROW_BYTES 80u
#define DOS_VGA_UPLOAD_MAX_ROWS 240u
#define DOS_VGA_UPLOAD_MAX_RUNS (4u * DOS_VGA_UPLOAD_MAX_ROWS)

struct DosVgaRun {
    unsigned short row, rows;
    unsigned char mask, plane;
};

/* Pure experimental planner: no allocation, I/O, or persistent state.
 * planes contains four consecutive plane_stride-byte planes. Each requested
 * row contains 80 bytes; rows <= 240 and plane_stride >= rows * 80.
 * Plane data may be unaligned. Caller supplies readable storage for all four
 * strides and disjoint, properly aligned runs storage for capacity entries.
 * capacity must be >= 4 * rows, even when fewer runs would suffice.
 * Return the run count (0 for zero rows with valid non-NULL pointers), or -1
 * for NULL pointers, invalid rows/stride, or insufficient capacity. Validation
 * completes before reading plane data or writing runs. Unused runs stay intact.
 * Runs are ordered by source plane, then row, and broadcast exact source bytes
 * to the mask's destination planes at the same row. Source plane is always the
 * lowest selected plane. Only pairs 0/1 and 2/3, or all four, are combined.
 */
int dos_vga_upload_plan(const unsigned char *planes, unsigned int plane_stride,
                        unsigned int rows, struct DosVgaRun *runs,
                        unsigned int capacity);

#endif
