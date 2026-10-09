/* DOS storage adapter for the pinned Towns heap API. */
#ifndef DOSCRAFT_HEAP_H
#define DOSCRAFT_HEAP_H

/* u32 in generated dos_compat.h is unsigned int. Keep this header usable by
 * native tests without importing Towns hw.h or requiring 32-bit pointers. */
typedef char dos_heap_u32_width[(sizeof(unsigned int) == 4) ? 1 : -1];

/* Bind borrowed, disjoint storage; no allocation-size or RAM-detection policy.
 * High storage starts on a 16-byte boundary. Both ends must be 16-byte aligned,
 * as in Towns sys_init. NULL with size zero disables that arena. Low storage
 * may start unaligned, like __bss_end. Return 0 on success, -1 for invalid
 * ranges or an already initialized heap; failure leaves the heap unchanged.
 * Storage must remain live until shutdown. Not interrupt/thread safe. */
int dos_heap_init(void *low, unsigned int low_size,
                  void *high, unsigned int high_size);
/* Detach storage and invalidate all allocations/marks. Caller releases it. */
void dos_heap_shutdown(void);

void *heap_alloc_low(unsigned int size);
void *heap_alloc_high(unsigned int size);
unsigned int heap_low_free(void);
unsigned int heap_high_free(void);
/* Marks are logical Towns high addresses: 0x100000 + raw bytes consumed,
 * including padding. They are opaque to callers, never DOS pointers. */
unsigned int heap_high_mark(void);
void heap_high_rewind(unsigned int mark);

/* Supplied by future DOS system/error handling. Towns fatal never returns.
 * The adapter also leaves state intact if a test/error hook does return. */
void fatal(const char *message);

#endif
