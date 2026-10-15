#ifndef DOSCRAFT_F2B_TEXEL_H
#define DOSCRAFT_F2B_TEXEL_H

#include <stdint.h>

#define DOS_F2B_TEXEL_MAX_JUMP 320

/* Experimental texture phase only: no texel reads, allocation or I/O.
 * Initialize once at the original row's first visible pixel, using its full
 * gradient and original clipping prestep. Never recompute for uncovered runs.
 * Fields are caller-owned; use init before jump. Sample in the caller at
 * base + ((uintptr_t)v_index << 8) + u_index.
 * With wrap, base is the tile pointer and indices are 0..15. Without wrap,
 * base is the initial address above bit 15; the two address bytes advance
 * independently, never carrying into each other or into base (trap.S ADCb).
 */
struct DosF2BTexel {
    uintptr_t base;
    uint16_t u_fraction, v_fraction, du_fraction, dv_fraction;
    uint8_t u_index, v_index, u_step, v_step, wrap;
};

/* u/v and du/dv are signed 16.16. Initial integer coordinates are masked
 * to 15 even without wrap, exactly as trap.S does before adding the tile.
 * wrap must be 0 or 1; tile must be non-NULL. Return 1 on success, -1 on
 * invalid arguments, preserving all old state bytes on rejection.
 * Tile/address validity and storage lifetime are the caller's responsibility.
 */
int dos_f2b_texel_init(struct DosF2BTexel *state, const uint8_t *tile,
                       int32_t u, int32_t v, int32_t du, int32_t dv, int wrap);

/* Advance exactly n original texels, including covered gaps. Return 1 for
 * 0 <= n <= 320 (zero is a no-op), -1 for NULL, invalid mode/indices/base,
 * or out-of-range n; rejection preserves the entire state.
 */
int dos_f2b_texel_jump(struct DosF2BTexel *state, int32_t n);

#endif
