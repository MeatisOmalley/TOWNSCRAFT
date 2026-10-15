#ifndef DOSCRAFT_EDGE_RECIP_H
#define DOSCRAFT_EDGE_RECIP_H

#include <limits.h>
#include <stdint.h>
#include "fmath.h"

#define DOS_EDGE_RECIP_MAX_DY 8192

extern uint32_t g_dos_edge_recip[DOS_EDGE_RECIP_MAX_DY + 1];

/* Call at raster target setup before divisions. Idempotent; DOS single thread. */
void dos_edge_recip_init(void);

/* Internal fast core: numerator <= INT_MAX, 1 <= dy <= 8192, table ready.
 * For d >= 2, M=ceil(2^32/d). Writing M=2^32/d+epsilon, 0<=epsilon<1,
 * gives n*M/2^32=n/d+n*epsilon/2^32, with error < 1/2.
 * Thus mulhigh is the true quotient or one too large. Its product with d
 * is <= n+d <= INT_MAX+8192 < 2^32, so a 32-bit correction is exact.
 */
static inline uint32_t dos_edge_recip_quotient(uint32_t numerator, uint32_t dy)
{
    uint32_t low, quotient;
    if (!numerator) return 0;
    /* 2^32 cannot be represented by the 32-bit reciprocal for d=1. */
    if (dy == 1) return numerator;
    __asm__("mull %3" : "=a"(low), "=d"(quotient)
            : "a"(numerator), "rm"(g_dos_edge_recip[dy]) : "cc");
    if (quotient * dy > numerator) --quotient;
    return quotient;
}

/* Exact original signed divshift, including truncation toward zero.
 * Only shifts 4/16 with a positive bounded divisor and a 31-bit shifted
 * unsigned magnitude use MULL. Unsigned negation handles INT_MIN safely.
 * All other inputs use the original IDIV; its no-overflow/nonzero-divisor
 * caller contract still applies. Validate the divisor even when value==0.
 */
static inline int dos_edge_recip_divshift(int value, int dy, int shift)
{
    uint32_t magnitude, quotient;
    if ((shift != 4 && shift != 16) ||
        (uint32_t)dy - 1u >= DOS_EDGE_RECIP_MAX_DY)
        return divshift(value, dy, shift);
    magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    if (magnitude > ((uint32_t)INT_MAX >> shift))
        return divshift(value, dy, shift);
    quotient = dos_edge_recip_quotient(magnitude << shift, (uint32_t)dy);
    return value < 0 ? -(int)quotient : (int)quotient;
}

#endif
