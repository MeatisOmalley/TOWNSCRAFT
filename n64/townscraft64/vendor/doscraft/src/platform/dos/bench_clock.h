#ifndef DOSCRAFT_BENCH_CLOCK_H
#define DOSCRAFT_BENCH_CLOCK_H
#include "timer_phase.h"
/* PIT channel 2 mode 2 decrements modulo 65536. The IRQ0 epoch resolves
 * multiple wraps; do not substitute host time or RDTSC (unavailable on 486). */
static inline unsigned int dos_bench_clock_delta(unsigned int ticks,
    unsigned int old_ticks,unsigned int count,unsigned int old_count)
{
    unsigned int delta=(old_count-count)&65535u;
    int wraps=((int)((ticks-old_ticks)*DOS_PIT_DIVISOR)-(int)delta+32768)/65536;
    return delta+(wraps>0 ? (unsigned int)wraps<<16 : 0);
}
#endif
