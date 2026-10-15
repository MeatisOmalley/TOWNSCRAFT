#ifndef DOSCRAFT_BENCH_TRANSPORT_H
#define DOSCRAFT_BENCH_TRANSPORT_H
/* Diagnostic builds only: sum guest PIT time, not host wall time. */
#ifdef DOSCRAFT_BENCH
unsigned int bench_us(void);
extern unsigned int dos_bench_pack, dos_bench_upload, dos_bench_wait;
extern unsigned int dos_bench_timeouts;
#endif
#endif
