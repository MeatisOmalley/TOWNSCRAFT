#include "heap.h"
#undef NDEBUG /* Zig release compilation must still execute the assertions. */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HIGH_START 0x100000u
#define LOW_START 0x20000u
static unsigned char low_storage[96] __attribute__((aligned(16)));
static unsigned char high_storage[160] __attribute__((aligned(16)));
static jmp_buf fatal_jump;
static int jumping, fatal_count;
static const char *fatal_message;

/* Compiled directly from the pinned sys.c heap section by test_heap.py. */
void reference_init(unsigned int low_start, unsigned int low_end,
                    unsigned int high_end);
void *reference_alloc_low(unsigned int size);
void *reference_alloc_high(unsigned int size);
unsigned int reference_low_free(void);
unsigned int reference_high_free(void);
unsigned int reference_high_mark(void);
void reference_high_rewind(unsigned int mark);

void fatal(const char *message)
{
    ++fatal_count;
    fatal_message = message;
    if (jumping) longjmp(fatal_jump, 1);
}

static void bind(unsigned int offset)
{
    dos_heap_shutdown();
    memset(low_storage, 0xa5, sizeof(low_storage));
    memset(high_storage, 0xa5, sizeof(high_storage));
    assert(dos_heap_init(low_storage + 16 + offset, 64 - offset,
                         high_storage + 16, 128) == 0);
    reference_init(LOW_START + offset, LOW_START + 64, HIGH_START + 128);
}

static void compare_state(void)
{
    assert(heap_low_free() == reference_low_free());
    assert(heap_high_free() == reference_high_free());
    assert(heap_high_mark() == reference_high_mark());
}

static void *allocate(int reference, int low, unsigned int size, int *failed)
{
    void *result = 0;
    jumping = 1;
    *failed = 0;
    if (setjmp(fatal_jump)) {
        *failed = 1;
        assert(strcmp(fatal_message, "Out of memory") == 0);
    } else if (reference) {
        result = low ? reference_alloc_low(size) : reference_alloc_high(size);
    } else {
        result = low ? heap_alloc_low(size) : heap_alloc_high(size);
    }
    jumping = 0;
    return result;
}

static void compare_allocation(int low, unsigned int size)
{
    int actual_failed, reference_failed;
    void *actual = allocate(0, low, size, &actual_failed);
    uintptr_t expected = (uintptr_t)allocate(1, low, size, &reference_failed);
    assert(actual_failed == reference_failed);
    if (!actual_failed) {
        void *mapped = expected >= HIGH_START
            ? high_storage + 16 + (expected - HIGH_START)
            : low_storage + 16 + (expected - LOW_START);
        assert(actual == mapped);
        assert(((uintptr_t)actual & 15u) == 0);
        if (size) memset(actual, 0x37, size);
    }
    compare_state();
}

static void rewind_checked(int reference, unsigned int mark, int *failed)
{
    jumping = 1;
    *failed = 0;
    if (setjmp(fatal_jump)) {
        *failed = 1;
        assert(strcmp(fatal_message, "Invalid temporary heap mark") == 0);
    } else if (reference) {
        reference_high_rewind(mark);
    } else {
        heap_high_rewind(mark);
    }
    jumping = 0;
}

static void compare_rewind(unsigned int mark)
{
    int actual_failed, reference_failed;
    rewind_checked(0, mark, &actual_failed);
    rewind_checked(1, mark, &reference_failed);
    assert(actual_failed == reference_failed);
    compare_state();
}

static void guards(unsigned int offset)
{
    unsigned int i;
    for (i = 0; i < 16 + offset; ++i) assert(low_storage[i] == 0xa5);
    for (i = 80; i < sizeof(low_storage); ++i) assert(low_storage[i] == 0xa5);
    for (i = 0; i < 16; ++i) assert(high_storage[i] == 0xa5);
    for (i = 144; i < sizeof(high_storage); ++i) assert(high_storage[i] == 0xa5);
}

static void parity(void)
{
    unsigned int offset, i, random = 0x48625u;
    for (offset = 0; offset < 16; ++offset) {
        unsigned int first, second;
        bind(offset);
        compare_state();
        compare_allocation(1, 0);
        compare_allocation(1, 1);
        compare_allocation(1, 17);
        compare_allocation(1, 64); /* Low fallback preserves low cursor. */
        first = heap_high_mark();
        compare_allocation(0, 1);
        second = heap_high_mark();
        compare_allocation(0, 0); /* Zero size still applies padding. */
        compare_rewind(second);
        compare_rewind(first);
        compare_rewind(second); /* Forward rewind is fatal. */
        compare_rewind(HIGH_START - 1);
        compare_rewind(HIGH_START);
        compare_allocation(0, 128); /* Exact fit. */
        compare_allocation(0, 0); /* End pointer is legal. */
        compare_allocation(0, 1); /* Exhaustion. */
        compare_rewind(HIGH_START + 1); /* Raw, unissued, unaligned mark. */
        compare_allocation(0, 112); /* Alignment consumes 15 bytes. */
        guards(offset);

        bind(offset);
        for (i = 0; i < 5000; ++i) {
            unsigned int choice;
            random = random * 1664525u + 1013904223u;
            choice = random >> 24;
            if ((choice & 7u) == 0) {
                unsigned int used = heap_high_mark() - HIGH_START;
                compare_rewind(HIGH_START + ((random >> 8) % (used + 1)));
            } else {
                compare_allocation(choice & 1u, (random >> 8) % 257);
            }
            guards(offset);
        }
    }
}

static void lifecycle(void)
{
    unsigned int mark, low_free, high_free;
    int before;
    dos_heap_shutdown();
    dos_heap_shutdown();
    assert(heap_low_free() == 0 && heap_high_free() == 0 && heap_high_mark() == 0);
    before = fatal_count;
    assert(heap_alloc_high(0) == 0);
    assert(heap_alloc_low(0) == 0);
    heap_high_rewind(HIGH_START);
    assert(fatal_count == before + 3);
    assert(strcmp(fatal_message, "Invalid temporary heap mark") == 0);

    assert(dos_heap_init(0, 1, high_storage, 128) == -1);
    assert(dos_heap_init(low_storage, 64, 0, 1) == -1);
    assert(dos_heap_init(low_storage, 63, high_storage, 128) == -1);
    assert(dos_heap_init(low_storage, 64, high_storage + 1, 127) == -1);
    assert(dos_heap_init(low_storage, 64, high_storage, 127) == -1);
    assert(dos_heap_init(low_storage, 64, low_storage + 16, 64) == -1);
    assert(dos_heap_init(0, 0, high_storage, UINT_MAX - 15u) == -1);
    assert(dos_heap_init((void *)(UINTPTR_MAX - 15u), 32, 0, 0) == -1);
    assert(heap_high_mark() == 0);

    bind(3);
    assert(heap_alloc_high(1) == high_storage + 16);
    mark = heap_high_mark();
    low_free = heap_low_free();
    high_free = heap_high_free();
    assert(dos_heap_init(0, 0, 0, 0) == -1); /* Live storage cannot be rebound. */
    before = fatal_count;
    assert(heap_alloc_high(UINT_MAX) == 0);
    assert(strcmp(fatal_message, "Out of memory") == 0);
    assert(heap_alloc_low(UINT_MAX) == 0);
    assert(strcmp(fatal_message, "Out of memory") == 0);
    heap_high_rewind(0);
    heap_high_rewind(mark + 1);
    assert(fatal_count == before + 4);
    assert(heap_high_mark() == mark);
    assert(heap_low_free() == low_free && heap_high_free() == high_free);
    assert(heap_alloc_high(1) == high_storage + 32);
    guards(3);
    dos_heap_shutdown();
    assert(high_storage[16] == 0xa5); /* Binding/shutdown never clears storage. */

    assert(dos_heap_init(0, 0, high_storage, 16) == 0);
    assert(heap_alloc_low(16) == high_storage);
    assert(heap_high_free() == 0);
    assert(heap_alloc_low(0) == high_storage + 16);
    dos_heap_shutdown();
    assert(dos_heap_init(low_storage, 16, 0, 0) == 0);
    assert(heap_alloc_low(16) == low_storage);
    assert(heap_alloc_high(1) == 0);
    dos_heap_shutdown();
    assert(dos_heap_init(low_storage, 16, low_storage + 16, 16) == 0);
    dos_heap_shutdown(); /* Adjacent ranges are legal. */
    assert(dos_heap_init(0, 0, high_storage, 0) == 0);
    assert(heap_alloc_high(0) == high_storage); /* Present empty range. */
    assert(heap_alloc_high(1) == 0);
    dos_heap_shutdown();
    assert(dos_heap_init(0, 0, 0, 0) == 0);
    assert(heap_alloc_high(0) == 0);
    dos_heap_shutdown();
}

int main(void)
{
    parity();
    lifecycle();
    puts("PASS: pinned Towns parity, 80000 trace steps, bounds, lifecycle, failure safety");
    return 0;
}
