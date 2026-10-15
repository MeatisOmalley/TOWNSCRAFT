/* N64 stand-in for DJGPP <dpmi.h>: memory needs no locking for interrupt use. */
#ifndef TOWNSCRAFT64_DPMI_H
#define TOWNSCRAFT64_DPMI_H
static inline int _go32_dpmi_lock_data(void *p, unsigned long n) { (void)p; (void)n; return 0; }
static inline int _go32_dpmi_lock_code(void *p, unsigned long n) { (void)p; (void)n; return 0; }
#endif
