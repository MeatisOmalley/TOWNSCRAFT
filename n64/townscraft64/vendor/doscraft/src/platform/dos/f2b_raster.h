#ifndef DOSCRAFT_F2B_RASTER_H
#define DOSCRAFT_F2B_RASTER_H

#include <stdint.h>

/* Bounded, single-threaded first-opaque backend. Callers must submit the
 * exact reversed painter primitive order; coverage contains no depth. */
struct DosF2BStats {
    unsigned sampled, skipped, rows, hidden_rows, hidden_primitives, covered;
};
extern struct DosF2BStats dos_f2b_stats;
extern int dos_f2b_active;

/* Record the ORIGINAL raster target. This never resets coverage or stats.
 * pitch is physical bytes/row; logical rows advance by pitch*scale.
 * begin validates dimensions (1..320, 1..200) and a matching valid recorded
 * target, clears stats/coverage and
 * returns 1 on success, 0 on rejection. Rejection leaves active disabled.
 * Active trap calls require a matching recorded target and stride/scale.
 * No allocation, floats, FPU or SIMD; performance unproven. Link f2b_span.S
 * for opaque spans; cutout spans use inline C byte/fraction advancement. */
void dos_f2b_set_target(unsigned char *fb, int w, int h, int pitch, int scale);
int dos_f2b_begin(int w, int h);
void dos_f2b_end(void);
int dos_f2b_full(void);
/* Exclusive bounds, clipped to the viewport; empty/reversed means false.
 * Query only: true increments hidden_primitives but never claims pixels. */
int dos_f2b_hidden_rect(int left, int top, int right, int bottom);
/* Claim BEFORE writing any point color, including zero. */
int dos_f2b_claim_point(int x, int y);
/* row is the logical row's physical start. Claims all actual flat writes,
 * including color zero, and duplicates horizontally at scale 2. Inactive
 * calls perform ordinary flat stores clipped to the recorded target. */
void dos_f2b_flat_row(unsigned char *row, int left, int right, int y,
                      int scale, unsigned char color);

/* Exact private raster.c / trap.S ABI. On DOS and native x86 this is 72B;
 * all offsets are checked when compiling f2b_raster.c. The linker wraps
 * the eight trap symbols; __real_* must resolve to the original assembly.
 * Invalid/off-target active rows make no writes, but advance Trap exactly.
 * A caller must not switch target geometry during an active frame. */
struct DosF2BTrap {
    unsigned char *row;
    int stride, rows;
    int lx, ldx, lu, ldu, lv, ldv;
    int rx, rdx, ru, rdu, rv, rdv;
    const unsigned char *tile;
    int maxX;
    uint32_t pixels;
};
#define DOS_F2B_WRAP_DECL(name) void __wrap_##name(struct DosF2BTrap *t)
DOS_F2B_WRAP_DECL(trap_opaque1);
DOS_F2B_WRAP_DECL(trap_opaque2);
DOS_F2B_WRAP_DECL(trap_transp1);
DOS_F2B_WRAP_DECL(trap_transp2);
DOS_F2B_WRAP_DECL(trap_opaque1w);
DOS_F2B_WRAP_DECL(trap_opaque2w);
DOS_F2B_WRAP_DECL(trap_transp1w);
DOS_F2B_WRAP_DECL(trap_transp2w);
#undef DOS_F2B_WRAP_DECL

#endif
