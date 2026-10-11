#ifndef DOSCRAFT_VGA_PROVEN_H
#define DOSCRAFT_VGA_PROVEN_H

#include "vga_upload_plan.h"

/* For rows with proof bit 0, every source pair (2*x,2*x+1) is identical.
 * With DOS_VGA_PROVEN_VERTICAL, bit 1 on an odd row also proves it is an
 * exact copy of the preceding even row, which must carry bit 0.
 * Other rows and the entire sharp HUD are arbitrary pixels. The caller owns
 * the proof; this helper never infers it from render scale alone. */
int dos_vga_proven_pack_plan(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven,
                             struct DosVgaRun *runs, unsigned int run_capacity);
int dos_vga_proven_pack_plan_view(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven,
                             struct DosVgaRun *runs, unsigned int run_capacity);

#ifdef DOS_LOW_PLANAR
/* planar[y/2] marks view row pairs whose planes 0 and 2 the renderer already
 * wrote at row y (even) of planes; they are doubled and classified, never
 * gathered from source. Other rows follow the proof rules above. view_only
 * leaves the HUD rows untouched. */
int dos_vga_proven_pack_plan_planar(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven, const unsigned char *planar,
                             struct DosVgaRun *runs, unsigned int run_capacity,
                             int view_only);
#endif

#endif
