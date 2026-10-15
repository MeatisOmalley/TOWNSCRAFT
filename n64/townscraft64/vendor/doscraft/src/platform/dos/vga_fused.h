#ifndef DOSCRAFT_VGA_FUSED_H
#define DOSCRAFT_VGA_FUSED_H
#include "vga_upload_plan.h"

/* Fuse 320x240 packing with exact broadcast classification of view rows 0..199.
 * Four 19200-byte destination planes, disjoint source/runs storage. Only view
 * bytes referenced by returned runs are authoritative; skipped duplicate
 * planes need not be written. All four HUD planes (rows 200..239) are complete.
 * Runs have the same owners/masks/order as dos_vga_upload_plan. No assumption
 * of doubled source pixels, even when the caller supplies a render-scale hint.
 * Source rows may be unaligned, with pitch >=320; all 240 rows must be readable.
 * Return run count, or -1 for invalid pointers/capacities/pitch, before writes.
 */
int dos_vga_pack_fused(const unsigned char *source,unsigned int pitch,
                       unsigned char *planes,unsigned int capacity,
                       struct DosVgaRun *runs,unsigned int run_capacity);
#endif
