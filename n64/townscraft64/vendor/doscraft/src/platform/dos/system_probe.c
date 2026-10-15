/* Automatic IRQ transport diagnostic, not a playable game or FPS benchmark.
 * Controller D2 injection tests the emulated AT controller -> PIC -> IRQ1 ->
 * unchanged key API. It does NOT prove Windows synthetic/physical input. */
#include "irq.h"
#include "keyboard.h"
#include "sys.h"
#include "system.h"
#include "heap.h"
#include "gfx.h"
#include "video_backend.h"
#include <dpmi.h>
#include <pc.h>
#include <sys/movedata.h>
#include <stdio.h>

extern u32 g_profCount, g_profSamples[4096];
extern int g_profEnable;
static volatile unsigned int hook_count;
static void tick_hook(void) { ++hook_count; }
static unsigned long bios_ticks(void)
{
    unsigned long tick;
    dosmemget(0x46C, sizeof(tick), &tick);
    return tick;
}
static int wait_ticks(unsigned int amount)
{
    unsigned int start = g_ticks, guard = 50000000;
    while (g_ticks - start < amount && --guard) {}
    return guard != 0;
}
static int wait_controller(void)
{
    unsigned int guard = 100000;
    while ((inportb(0x64) & 2) && --guard) {}
    return guard != 0;
}
static int inject(unsigned char byte)
{
    unsigned int start = dos_irq_keyboard_count, guard = 1000000;
    if (!wait_controller()) return 0;
    outportb(0x64, 0xD2);
    if (!wait_controller()) return 0;
    outportb(0x60, byte);
    while (dos_irq_keyboard_count == start && --guard) {}
    return guard != 0;
}
static int keyboard_test(void)
{
    key_flush();
    if (!inject(0x12) || key_get_event() != KEY_E || !g_keyDown[KEY_E]) return 0;
    if (!inject(0x92) || g_keyDown[KEY_E] || key_get_event() != -1) return 0;
    if (!inject(0xE0) || !inject(0x48) || key_get_event() != KEY_UP || !g_keyDown[KEY_UP]) return 0;
    if (!inject(0xE0) || !inject(0xC8) || g_keyDown[KEY_UP]) return 0;
    if (!inject(0x2A) || !inject(0x36) || !inject(0xAA) || !g_keyDown[KEY_SHIFT]) return 0;
    if (!inject(0xB6) || g_keyDown[KEY_SHIFT]) return 0;
    key_flush();
    return key_get_event() == -1;
}
static int guards_test(void)
{
    unsigned long first, second;
    int ok;
    first = dos_keyboard_irq_save();
    second = dos_keyboard_irq_save();
    key_flush();
    ok = !__dpmi_get_virtual_interrupt_state() && !second;
    dos_keyboard_irq_restore(second);
    ok &= !__dpmi_get_virtual_interrupt_state();
    dos_keyboard_irq_restore(first);
    return ok && __dpmi_get_virtual_interrupt_state() == (int)first;
}
static int same_vector(__dpmi_paddr *a, __dpmi_paddr *b)
{
    return a->offset32 == b->offset32 && a->selector == b->selector;
}
int main(void)
{
    __dpmi_version_ret version;
    __dpmi_paddr old_timer, old_key, timer, key;
    unsigned long bios_start, bios_delta, state;
    unsigned int start, elapsed, chains, samples, interrupts, i;
    unsigned char pic = inportb(0x21);
    int timer_ok, key_ok, guard_ok, restore_ok, hook_ok, profiler_ok, second_ok;
    int heap_ok, video_ok = 1;
    unsigned char *permanent, *temporary, *again;
    unsigned int mark, low_free, high_free;
    FILE *report;
    puts("DOScraft 100 Hz timer / AT IRQ1 diagnostic (not the game)");
    __dpmi_get_version(&version);
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic, &old_timer);
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic + 1, &old_key);
    sys_init();
    low_free = heap_low_free();
    high_free = heap_high_free();
    permanent = heap_alloc_low(800000);
    mark = heap_high_mark();
    temporary = heap_alloc_high(5300000);
    for (i = 0; i < 800000; i += 4096) permanent[i] = (unsigned char)(i >> 12);
    for (i = 0; i < 5300000; i += 4096) temporary[i] = (unsigned char)(i >> 12);
    heap_high_rewind(mark);
    again = heap_alloc_high(5300000);
    heap_ok = again == temporary && g_ramMB == 16 && low_free == 1048576 && high_free == 8388608;
    for (i = 0; i < 800000; i += 4096) heap_ok &= permanent[i] == (unsigned char)(i >> 12);
    for (i = 0; i < 5300000; i += 4096) heap_ok &= again[i] == (unsigned char)(i >> 12);
    heap_high_rewind(mark);
    guard_ok = guards_test();
    key_ok = keyboard_test();
    state = dos_keyboard_irq_save();
    g_profCount = hook_count = 0;
    g_profEnable = 1;
    dos_irq_set_tick_hook(tick_hook);
    start = g_ticks;
    chains = dos_irq_bios_count;
    bios_start = bios_ticks();
    dos_keyboard_irq_restore(state);
    timer_ok = wait_ticks(200);
    state = dos_keyboard_irq_save();
    elapsed = g_ticks - start;
    bios_delta = bios_ticks() - bios_start;
    chains = dos_irq_bios_count - chains;
    samples = g_profCount;
    interrupts = dos_irq_keyboard_count;
    hook_ok = hook_count == elapsed;
    g_profEnable = 0;
    dos_irq_set_tick_hook(0);
    dos_keyboard_irq_restore(state);
    timer_ok &= elapsed == 200 && chains >= 35 && chains <= 38 && bios_delta == chains;
    profiler_ok = samples > 0 && samples <= elapsed;
    for (i = 0; i < samples; ++i) if (!g_profSamples[i]) profiler_ok = 0;
    /* Exercise VGA while the custom PIT runs: libc uclock assumes the BIOS
     * divisor, so the presentation timeout must now use g_ticks instead. */
    video_init();
    gfx_init();
    for (i = 0; i < 3; ++i) {
        unsigned char *drawn = g_fb;
        gfx_clear(drawn, (unsigned char)(i + 1));
        gfx_present();
        video_ok &= !dos_video_verify(drawn, FB_PITCH);
    }
    dos_video_restore();
    dos_system_shutdown();
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic, &timer);
    __dpmi_get_protected_mode_interrupt_vector(version.master_pic + 1, &key);
    restore_ok = same_vector(&old_timer, &timer) && same_vector(&old_key, &key) && inportb(0x21) == pic;
    /* Reinstall/cleanup again: validate lifecycle, then check BIOS time still
     * advances after cleanup rather than merely comparing vector addresses. */
    second_ok = !dos_irq_init();
    if (second_ok) second_ok = wait_ticks(5);
    dos_irq_shutdown();
    bios_start = bios_ticks();
    for (i = 0; i < 50000000 && bios_ticks() - bios_start < 3; ++i) {}
    restore_ok &= bios_ticks() - bios_start >= 3;
    report = fopen("C:\\SYSTEM.TXT", "wb");
    if (!report) return 1;
    fprintf(report, "DOScraft DOS interrupt diagnostic\r\nTIMER_TICKS=%u\r\nBIOS_TICKS=%lu\r\n"
            "BIOS_CHAINS=%u\r\nKEYBOARD_IRQS=%u\r\nPROFILER_SAMPLES=%u\r\n"
            "TIMER_100HZ=%s\r\nKEYBOARD_CONTROLLER_IRQ=%s\r\nINTERRUPT_GUARDS=%s\r\n"
            "TICK_HOOK=%s\r\nPROFILER=%s\r\nRESTORE=%s\r\nREINSTALL=%s\r\n"
            "RAM_MB=%u\r\nLOW_BYTES=%u\r\nHIGH_BYTES=%u\r\nHEAP_STARTUP=%s\r\nVIDEO_WITH_IRQ=%s\r\nRESULT=%s\r\n",
            elapsed, bios_delta, chains, interrupts, samples,
            timer_ok ? "PASS" : "FAIL", key_ok ? "PASS" : "FAIL", guard_ok ? "PASS" : "FAIL",
            hook_ok ? "PASS" : "FAIL", profiler_ok ? "PASS" : "FAIL", restore_ok ? "PASS" : "FAIL",
            second_ok ? "PASS" : "FAIL",
            g_ramMB, low_free, high_free, heap_ok ? "PASS" : "FAIL", video_ok ? "PASS" : "FAIL",
            timer_ok && key_ok && guard_ok && hook_ok && profiler_ok && restore_ok && second_ok && heap_ok && video_ok ? "PASS" : "FAIL");
    fclose(report);
    puts(timer_ok && key_ok && guard_ok && hook_ok && profiler_ok && restore_ok && second_ok && heap_ok && video_ok ?
         "DOS interrupts: PASS" : "DOS interrupts: FAIL (see C:\\SYSTEM.TXT)");
    return !(timer_ok && key_ok && guard_ok && hook_ok && profiler_ok && restore_ok && second_ok && heap_ok && video_ok);
}
