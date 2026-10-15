#ifndef DOSCRAFT_TIMER_PHASE_H
#define DOSCRAFT_TIMER_PHASE_H
/* Rounded AT PIT divisor: 1193182 / 11932 = 99.9985 Hz. Keep BIOS time at
 * its original 65536-clock period, not at the game's interrupt frequency. */
#define DOS_PIT_DIVISOR 11932u
static inline int dos_timer_bios_step(unsigned int *phase)
{
    *phase += DOS_PIT_DIVISOR;
    if (*phase >= 65536u) {
        *phase -= 65536u;
        return 1;
    }
    return 0;
}
#endif
