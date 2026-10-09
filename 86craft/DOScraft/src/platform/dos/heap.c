#include "heap.h"
#include <stdint.h>
#include <limits.h>

#define HIGH_START 0x100000u

struct Arena {
    unsigned char *base;
    unsigned int size, used;
};
static struct Arena low_arena, high_arena;
static int initialized;

static int valid_range(void *base, unsigned int size)
{
    uintptr_t start = (uintptr_t)base;
    if (!base) return size == 0;
    if (size > UINTPTR_MAX - start) return 0;
    return ((start + size) & 15u) == 0;
}

int dos_heap_init(void *low, unsigned int low_size,
                  void *high, unsigned int high_size)
{
    uintptr_t lo = (uintptr_t)low, hi = (uintptr_t)high;
    if (initialized || !valid_range(low, low_size) ||
        !valid_range(high, high_size) || (hi & 15u) ||
        high_size > UINT_MAX - HIGH_START) return -1;
    if (low_size && high_size && lo < hi + high_size && hi < lo + low_size)
        return -1;
    low_arena.base = low;
    low_arena.size = low_size;
    low_arena.used = 0;
    high_arena.base = high;
    high_arena.size = high_size;
    high_arena.used = 0;
    initialized = 1;
    return 0;
}

void dos_heap_shutdown(void)
{
    low_arena.base = high_arena.base = 0;
    low_arena.size = low_arena.used = 0;
    high_arena.size = high_arena.used = 0;
    initialized = 0;
}

static unsigned int aligned_used(const struct Arena *arena)
{
    /* Aligned ends guarantee padding fits, even at the end of an arena. */
    uintptr_t address = (uintptr_t)arena->base + arena->used;
    return arena->used + (unsigned int)((0u - address) & 15u);
}

void *heap_alloc_low(unsigned int size)
{
    unsigned int p = aligned_used(&low_arena);
    if (!initialized || !low_arena.base || size > low_arena.size - p)
        return heap_alloc_high(size);
    low_arena.used = p + size;
    return low_arena.base + p;
}

void *heap_alloc_high(unsigned int size)
{
    unsigned int p = aligned_used(&high_arena);
    /* Subtraction avoids the original address+size wraparound for malformed
     * huge requests. All in-range Towns allocations retain their behavior. */
    if (!initialized || !high_arena.base || size > high_arena.size - p) {
        fatal("Out of memory");
        return 0;
    }
    high_arena.used = p + size;
    return high_arena.base + p;
}

unsigned int heap_low_free(void)
{
    return low_arena.size - aligned_used(&low_arena);
}

unsigned int heap_high_free(void)
{
    return high_arena.size - aligned_used(&high_arena);
}

unsigned int heap_high_mark(void)
{
    return initialized ? HIGH_START + high_arena.used : 0;
}

void heap_high_rewind(unsigned int mark)
{
    if (!initialized || mark < HIGH_START || mark > HIGH_START + high_arena.used) {
        fatal("Invalid temporary heap mark");
        return;
    }
    /* Like Towns, accept any in-range raw mark, including unaligned marks. */
    high_arena.used = mark - HIGH_START;
}
