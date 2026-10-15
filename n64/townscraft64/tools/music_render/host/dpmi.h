/* Host stand-in for DJGPP <dpmi.h>. */
static inline int _go32_dpmi_lock_data(void *p, unsigned long n) { (void)p; (void)n; return 0; }
