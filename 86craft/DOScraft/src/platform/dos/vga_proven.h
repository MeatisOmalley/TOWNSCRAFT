#ifndef DOSCRAFT_VGA_PROVEN_H
#define DOSCRAFT_VGA_PROVEN_H

#include "vga_upload_plan.h"

/* For rows marked proven, every source pair (2*x,2*x+1) is identical.
 * Other rows and the entire sharp HUD are arbitrary pixels. The caller owns
 * the proof; this helper never infers it from render scale alone. */
int dos_vga_proven_pack_plan(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven,
                             struct DosVgaRun *runs, unsigned int run_capacity);

#endif
