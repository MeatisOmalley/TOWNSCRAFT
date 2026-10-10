/* Host-only tests of the real C body. All DPMI, port I/O, entry addresses,
 * atexit, and abort are mocked. This is not DOS/guest/hardware execution. */
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(2); \
} } while (0)
static int mock_atexit(void (*callback)(void));
static void mock_abort(void);
#define atexit mock_atexit
#define abort mock_abort
#include "irq.c"
#undef atexit
#undef abort

/* Match irq.c's undecorated linker-end symbol without modifying its source. */
char test_image_end __asm__("end");
char test_code_end __asm__("etext"); /* Mock linker text bound, not host code. */
static const unsigned char initial_mask = 0xA6;
static unsigned char pic_mask = 0xA6, controller_status, controller_byte;
static int irq_state = 1, pending, reads, resets, feeds, eois;
static unsigned char last_byte;
static int lock_calls, unlock_calls, locked, exit_calls, hook_calls;
static int version_calls, get_calls, segment_calls, set_calls;
static int abort_expected, abort_calls;
static const char *failure = "";
static jmp_buf abort_target;
static void (*exit_callback)(void);
static __dpmi_meminfo lock_info;
static __dpmi_paddr vectors[2] = {{0x12345678, 0x123}, {0x23456789, 0x234}};
static const __dpmi_paddr originals[2] = {{0x12345678, 0x123}, {0x23456789, 0x234}};
enum { MASTER = 0x50, DATA_SELECTOR = 0x28, CODE_SELECTOR = 0x30 };
enum { DISABLE, ENABLE, LOCK, UNLOCK, EXIT_REGISTER, GET_VECTOR, SET_VECTOR,
       PIC, PIT_COMMAND, PIT_BYTE, STATUS_READ, DATA_READ, RESET, FEED, EOI, ABORT };
static struct { int kind; unsigned long value; } events[4096];
static unsigned int event_count;

static void event(int kind, unsigned long value)
{
    CHECK(event_count < sizeof(events) / sizeof(events[0]));
    events[event_count].kind = kind;
    events[event_count++].value = value;
}
static int fails(const char *name) { return strcmp(failure, name) == 0; }
static int same(__dpmi_paddr a, __dpmi_paddr b)
{ return a.offset32 == b.offset32 && a.selector == b.selector; }
static int live_vectors(void)
{ return !same(vectors[0], originals[0]) || !same(vectors[1], originals[1]); }
static int first_event(int kind)
{
    unsigned int i;
    for (i = 0; i < event_count; ++i) if (events[i].kind == kind) return (int)i;
    return -1;
}
static void clear_events(void) { event_count = 0; }

unsigned short _my_ds(void) { return DATA_SELECTOR; }
unsigned short _my_cs(void) { return CODE_SELECTOR; }
int __dpmi_get_version(__dpmi_version_ret *version)
{
    ++version_calls;
    version->master_pic = MASTER;
    return fails("version") ? -1 : 0;
}
int __dpmi_get_protected_mode_interrupt_vector(int vector, __dpmi_paddr *address)
{
    CHECK(vector == MASTER || vector == MASTER + 1);
    ++get_calls;
    event(GET_VECTOR, (unsigned int)vector);
    if (fails(vector == MASTER ? "get_timer" : "get_keyboard")) return -1;
    *address = vectors[vector - MASTER];
    return 0;
}
int __dpmi_get_segment_base_address(int selector, unsigned long *base)
{
    CHECK(selector == DATA_SELECTOR);
    ++segment_calls;
    *base = 0x100000;
    return fails("segment") ? -1 : 0;
}
int __dpmi_lock_linear_region(__dpmi_meminfo *info)
{
    ++lock_calls;
    CHECK(info->address == 0x101000);
    CHECK(info->size == (unsigned long)&test_image_end - 0x1000);
    CHECK(!locked && !live_vectors());
    lock_info = *info;
    event(LOCK, info->size);
    if (fails("lock")) return -1;
    locked = 1;
    return 0;
}
int __dpmi_unlock_linear_region(__dpmi_meminfo *info)
{
    CHECK(locked && !live_vectors()); /* Never free beneath an installed ISR. */
    CHECK(info->address == lock_info.address && info->size == lock_info.size);
    ++unlock_calls;
    locked = 0;
    event(UNLOCK, 0);
    return 0;
}
int __dpmi_get_and_disable_virtual_interrupt_state(void)
{
    int old = irq_state;
    irq_state = 0;
    event(DISABLE, (unsigned int)old);
    return old;
}
int __dpmi_get_and_set_virtual_interrupt_state(int state)
{
    int old = irq_state;
    CHECK(state == 0 || state == 1);
    irq_state = state;
    event(ENABLE, (unsigned int)state);
    return old;
}
int __dpmi_set_protected_mode_interrupt_vector(int vector, __dpmi_paddr *address)
{
    int slot = vector - MASTER, restoring;
    CHECK(slot == 0 || slot == 1);
    CHECK(locked && irq_state == 0 && (pic_mask & 3) == 3);
    restoring = same(*address, originals[slot]);
    if (!restoring) {
        CHECK(address->selector == CODE_SELECTOR);
        CHECK(address->offset32 == (unsigned long)(slot ? dos_irq_keyboard_entry : dos_irq_timer_entry));
    }
    ++set_calls;
    event(SET_VECTOR, (unsigned int)slot + (restoring ? 2u : 0u));
    if ((!restoring && fails(slot ? "install_keyboard" : "install_timer")) ||
        (restoring && fails(slot ? "restore_keyboard" : "restore_timer")) ||
        (fails("rollback_timer") && (slot == 1 || restoring))) return -1;
    vectors[slot] = *address;
    return 0;
}
unsigned char inportb(unsigned short port)
{
    CHECK(irq_state == 0);
    if (port == 0x21) return pic_mask;
    if (port == 0x64) {
        event(STATUS_READ, 0);
        return pending >= 0 ? (pending ? 1 : 0) : controller_status;
    }
    CHECK(port == 0x60);
    ++reads;
    event(DATA_READ, 0);
    if (pending > 0) --pending;
    return controller_byte;
}
void outportb(unsigned short port, unsigned char byte)
{
    CHECK(irq_state == 0);
    switch (port) {
    case 0x21: pic_mask = byte; event(PIC, byte); break;
    case 0x43: CHECK(byte == 0x36); event(PIT_COMMAND, byte); break;
    case 0x40: event(PIT_BYTE, byte); break;
    case 0x20: CHECK(byte == 0x20); ++eois; event(EOI, byte); break;
    default: CHECK(0); /* No controller commands or extra hardware writes. */
    }
}
void dos_keyboard_reset(void)
{ CHECK(irq_state == 0); ++resets; event(RESET, 0); }
void dos_keyboard_scancode(unsigned char byte)
{ CHECK(irq_state == 0); ++feeds; last_byte = byte; event(FEED, byte); }
void dos_irq_timer_entry(void) { CHECK(0); }
void dos_irq_keyboard_entry(void) { CHECK(0); }
static int mock_atexit(void (*callback)(void))
{
    CHECK(locked && !live_vectors());
    ++exit_calls;
    event(EXIT_REGISTER, 0);
    CHECK(callback == dos_irq_shutdown);
    if (fails("atexit")) return -1;
    exit_callback = callback;
    return 0;
}
static void mock_abort(void)
{
    CHECK(abort_expected && locked && live_vectors() && irq_state == 0);
    ++abort_calls;
    event(ABORT, 0);
    longjmp(abort_target, 1);
}
static void hook(void)
{
    ++hook_calls;
    CHECK(irq_state == 0);
    CHECK(g_ticks == (unsigned int)hook_calls); /* Tick advances before callback. */
}

static void check_pit(unsigned int divisor)
{
    int index = first_event(PIT_COMMAND);
    CHECK(index >= 0 && (unsigned int)index + 2 < event_count);
    CHECK(events[index + 1].kind == PIT_BYTE && events[index + 1].value == (divisor & 255));
    CHECK(events[index + 2].kind == PIT_BYTE && events[index + 2].value == (divisor >> 8));
}
static void check_clean(int state)
{
    CHECK(!locked && !live_vectors() && irq_state == state && pic_mask == initial_mask);
}
static void test_phase(void)
{
    static const unsigned int seeds[] = {0, 1, 53603, 53604, 65535};
    unsigned int seed, i, phase;
    CHECK(sizeof(unsigned int) == 4 && sizeof(unsigned long) == 4 && sizeof(void *) == 4);
    CHECK(TICKS_PER_SEC == 100 && DOS_PIT_DIVISOR == 11932u);
    for (seed = 0; seed < 65536; ++seed) {
        phase = seed;
        CHECK(dos_timer_bios_step(&phase) == (seed + 11932u >= 65536u));
        CHECK(phase == (seed + 11932u) % 65536u);
    }
    for (seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed) {
        uint64_t chains = 0;
        phase = seeds[seed];
        for (i = 1; i <= 1000000; ++i) {
            uint64_t total = (uint64_t)seeds[seed] + (uint64_t)i * 11932u;
            int step = dos_timer_bios_step(&phase);
            CHECK(step == 0 || step == 1);
            chains += (unsigned int)step;
            CHECK(chains == total / 65536u && phase == total % 65536u);
        }
    }
    phase = 0;
    seed = 0;
    for (i = 0; i < 16384; ++i) seed += (unsigned int)dos_timer_bios_step(&phase);
    CHECK(phase == 0 && seed == 2983); /* Exact rational cadence cycle. */
}
static void test_timer(void)
{
    unsigned int i, chains = 0;
    CHECK(dos_irq_init() == 0);
    clear_events();
    dos_irq_set_tick_hook(hook);
    CHECK(irq_state == 1 && event_count == 2);
    irq_state = 0; /* Model the assembly's exclusion, not actual IRQ execution. */
    clear_events();
    for (i = 1; i <= 1000000; ++i) {
        uint64_t total = (uint64_t)i * 11932u;
        int result = dos_irq_timer_body(0x100000 + i, CODE_SELECTOR);
        CHECK(result == 0 || result == 1);
        chains += (unsigned int)result;
        CHECK(g_ticks == i && hook_calls == (int)i);
        CHECK(chains == total / 65536u && dos_irq_bios_count == chains);
    }
    CHECK(event_count == 0 && g_profCount == 0 && eois == 0);
    dos_irq_set_tick_hook(0);
    clear_events();
    g_ticks = UINT_MAX - 1;
    dos_irq_timer_body(0, CODE_SELECTOR);
    CHECK(g_ticks == UINT_MAX);
    dos_irq_timer_body(0, CODE_SELECTOR);
    CHECK(g_ticks == 0 && hook_calls == 1000000);
    g_profEnable = 1;
    dos_irq_timer_body(0xDEAD, CODE_SELECTOR + 8);
    CHECK(g_profCount == 0); /* Host/BIOS selector must never be profiled. */
    dos_irq_timer_body(0, CODE_SELECTOR);
    dos_irq_timer_body(0xFFF, CODE_SELECTOR);
    dos_irq_timer_body((unsigned int)&test_code_end, CODE_SELECTOR);
    dos_irq_timer_body(UINT_MAX, CODE_SELECTOR);
    dos_irq_timer_body(0x1000, 0);
    CHECK(g_profCount == 0); /* Reject transport sentinel and non-text offsets. */
    dos_irq_timer_body((unsigned int)&test_code_end - 1, CODE_SELECTOR);
    CHECK(g_profCount == 1 && g_profSamples[0] == (unsigned int)&test_code_end - 1);
    g_profCount = 0;
    for (i = 0; i < 4096; ++i) {
        dos_irq_timer_body(0x1000 + i, CODE_SELECTOR | 0xCAFE0000u);
        CHECK(g_profCount == i + 1 && g_profSamples[i] == 0x1000 + i);
    }
    dos_irq_timer_body(0x1800, CODE_SELECTOR);
    CHECK(g_profCount == 4096 && g_profSamples[4095] == 0x1FFF);
    g_profCount = 4097; /* Defensive bound also rejects already invalid count. */
    dos_irq_timer_body(0x1800, CODE_SELECTOR);
    CHECK(g_profCount == 4097 && g_profSamples[4095] == 0x1FFF);
    g_profEnable = 0;
    g_profCount = 0;
    dos_irq_timer_body(0x1800, CODE_SELECTOR);
    CHECK(g_profCount == 0 && event_count == 0);
    dos_irq_shutdown();
    check_clean(0);
}
static void test_keyboard(void)
{
    unsigned int status;
    irq_state = 0;
    pending = -1;
    for (status = 0; status < 256; ++status) {
        int accepted = (status & 1) && !(status & 0xE0);
        reads = feeds = eois = 0;
        dos_irq_keyboard_count = 0;
        controller_status = (unsigned char)status;
        controller_byte = (unsigned char)(status ^ 0xA5);
        clear_events();
        dos_irq_keyboard_body();
        CHECK(reads == (int)(status & 1) && feeds == accepted && eois == 1);
        CHECK(dos_irq_keyboard_count == (unsigned int)accepted);
        CHECK(events[0].kind == STATUS_READ && events[event_count - 1].kind == EOI);
        CHECK(event_count == 2u + (status & 1) + (unsigned int)accepted);
        if (accepted) CHECK(last_byte == controller_byte);
    }
}
static void test_lifecycle(int state, int count)
{
    int sets, locks, unlocks;
    unsigned int before;
    irq_state = state;
    pending = count;
    dos_irq_shutdown(); /* Safe even before first initialization. */
    check_clean(state);
    clear_events();
    g_ticks = g_profCount = dos_irq_keyboard_count = dos_irq_bios_count = 99;
    g_profEnable = 1;
    CHECK(dos_irq_init() == 0);
    CHECK(locked && live_vectors() && irq_state == state && pic_mask == (initial_mask & ~3));
    CHECK(dos_irq_data_selector == DATA_SELECTOR && dos_irq_code_selector == CODE_SELECTOR);
    CHECK(g_ticks == 0 && g_profCount == 0 && !g_profEnable);
    CHECK(dos_irq_keyboard_count == 0 && dos_irq_bios_count == 0);
    CHECK(reads == (count < 32 ? count : 32) && pending == (count > 32 ? count - 32 : 0));
    CHECK(resets == 1 && set_calls == 2 && lock_calls == 1 && exit_calls == 1);
    CHECK(first_event(LOCK) < first_event(EXIT_REGISTER));
    CHECK(first_event(EXIT_REGISTER) < first_event(DISABLE));
    CHECK(first_event(PIC) < first_event(RESET));
    CHECK(first_event(RESET) < first_event(SET_VECTOR));
    CHECK(first_event(SET_VECTOR) < first_event(PIT_COMMAND));
    check_pit(11932);
    before = event_count;
    CHECK(dos_irq_init() == -1 && event_count == before); /* Double init guard. */
    dos_irq_set_tick_hook(hook);
    CHECK(irq_state == state);
    clear_events();
    dos_irq_shutdown();
    check_clean(state);
    CHECK(set_calls == 4 && unlock_calls == 1 && resets == 2);
    CHECK(first_event(PIC) < first_event(PIT_COMMAND));
    CHECK(first_event(PIT_COMMAND) < first_event(SET_VECTOR));
    CHECK(first_event(SET_VECTOR) < first_event(RESET));
    CHECK(first_event(ENABLE) < first_event(UNLOCK));
    check_pit(0);
    irq_state = 0;
    dos_irq_timer_body(0x1000, CODE_SELECTOR);
    CHECK(hook_calls == 0); /* Shutdown itself clears the installed callback. */
    irq_state = state;
    sets = set_calls; locks = lock_calls; unlocks = unlock_calls;
    dos_irq_shutdown();
    CHECK(set_calls == sets && lock_calls == locks && unlock_calls == unlocks);
    pending = 0;
    CHECK(dos_irq_init() == 0 && exit_calls == 1 && lock_calls == locks + 1);
    irq_state = 0;
    clear_events();
    CHECK(dos_irq_timer_body(0, CODE_SELECTOR) == 0 && g_ticks == 1);
    CHECK(hook_calls == 0 && g_profCount == 0 && dos_irq_bios_count == 0);
    irq_state = state;
    CHECK(exit_callback != NULL);
    exit_callback();
    check_clean(state);
}
static void test_init_failure(const char *name, int state)
{
    failure = name;
    irq_state = state;
    if (fails("unix_sbrk")) _crt0_startup_flags = _CRT0_FLAG_UNIX_SBRK;
    CHECK(dos_irq_init() == -1);
    check_clean(state);
    CHECK(first_event(PIT_COMMAND) == -1 && resets <= 1);
    if (fails("unix_sbrk")) CHECK(version_calls == 0 && event_count == 0);
    if (fails("version")) CHECK(get_calls == 0 && lock_calls == 0);
    if (fails("get_timer")) CHECK(get_calls == 1 && segment_calls == 0);
    if (fails("get_keyboard")) CHECK(get_calls == 2 && segment_calls == 0);
    if (fails("segment")) CHECK(segment_calls == 1 && lock_calls == 0);
    if (fails("lock")) CHECK(lock_calls == 1 && exit_calls == 0 && unlock_calls == 0);
    if (fails("atexit")) CHECK(lock_calls == 1 && exit_calls == 1 && unlock_calls == 1 && set_calls == 0);
    if (fails("install_timer")) CHECK(set_calls == 1 && unlock_calls == 1);
    if (fails("install_keyboard")) {
        CHECK(set_calls == 3 && unlock_calls == 1);
        CHECK(events[first_event(SET_VECTOR) + 2].kind == SET_VECTOR);
        CHECK(events[first_event(SET_VECTOR) + 2].value == 2); /* Timer rolled back. */
    }
    if (!fails("atexit") && !fails("install_timer") && !fails("install_keyboard"))
        CHECK(set_calls == 0 && first_event(PIC) == -1);
    failure = "";
    _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY;
    clear_events();
    CHECK(dos_irq_init() == 0); /* Every recoverable failure permits retry. */
    CHECK(exit_calls == (strcmp(name, "atexit") == 0 ? 2 : 1));
    dos_irq_shutdown();
    check_clean(state);
}
static void test_abort_safety(const char *name)
{
    if (strcmp(name, "rollback_timer") != 0) CHECK(dos_irq_init() == 0);
    clear_events();
    failure = name;
    abort_expected = 1;
    if (setjmp(abort_target) == 0) {
        if (fails("rollback_timer")) dos_irq_init();
        else dos_irq_shutdown();
        CHECK(0); /* A failed restoration must not return and unlock. */
    }
    CHECK(abort_calls == 1 && locked && live_vectors() && unlock_calls == 0);
    CHECK(irq_state == 0 && (pic_mask & 3) == 3);
    CHECK(events[event_count - 1].kind == ABORT && first_event(UNLOCK) == -1);
}
int main(int argc, char **argv)
{
    CHECK(_crt0_startup_flags == (_CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY));
    CHECK(argc >= 2);
    if (strcmp(argv[1], "phase") == 0) test_phase();
    else if (strcmp(argv[1], "timer") == 0) test_timer();
    else if (strcmp(argv[1], "keyboard") == 0) test_keyboard();
    else if (strcmp(argv[1], "lifecycle") == 0) {
        CHECK(argc == 4); test_lifecycle(atoi(argv[2]), atoi(argv[3]));
    } else if (strcmp(argv[1], "init_failure") == 0) {
        CHECK(argc == 4); test_init_failure(argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "abort_safety") == 0) {
        CHECK(argc == 3); test_abort_safety(argv[2]);
    } else CHECK(0);
    printf("PASS: %s (host mocks only; no guest/hardware proof)\n", argv[1]);
    return 0;
}
