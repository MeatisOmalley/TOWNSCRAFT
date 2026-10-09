/* PM-only hardware hooks. See DJGPP FAQ 18.9 / 18.11. All ISR code/data,
 * including the translator and the two private stacks, are explicitly locked.
 * The diagnostic launch uses CWSDPMI -s-: disk paging is not permitted. */
#include "irq.h"
#include "keyboard.h"
#include "sys.h"
#include "timer_phase.h"
#include <crt0.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdlib.h>

volatile u32 g_ticks;
u32 g_profSamples[4096], g_profCount;
int g_profEnable;
volatile unsigned int dos_irq_keyboard_count, dos_irq_bios_count;
unsigned short dos_irq_data_selector, dos_irq_code_selector;
__dpmi_paddr dos_irq_old_timer;
static __dpmi_paddr old_keyboard;
static __dpmi_meminfo locked_image;
static unsigned int bios_phase;
static unsigned char old_pic_mask;
static int timer_vector, keyboard_vector, installed, image_locked, exit_registered;
static void (*tick_hook)(void);
extern void dos_irq_timer_entry(void), dos_irq_keyboard_entry(void);
extern char image_end __asm__("end");
extern char code_end __asm__("etext");

unsigned long dos_keyboard_irq_save(void)
{
    unsigned long state;
    __asm__ volatile("" ::: "memory");
    state = __dpmi_get_and_disable_virtual_interrupt_state();
    __asm__ volatile("" ::: "memory");
    return state;
}

void dos_keyboard_irq_restore(unsigned long state)
{
    __asm__ volatile("" ::: "memory");
    __dpmi_get_and_set_virtual_interrupt_state((int)state);
    __asm__ volatile("" ::: "memory");
}

/* Assembly passes the interrupted CS:EIP before switching stacks. Unlike the
 * bare-metal Towns, DOS can interrupt BIOS/host code: don't label its offsets
 * as game addresses. No x87 operations are allowed in either ISR. */
int dos_irq_timer_body(unsigned int eip, unsigned int cs)
{
    ++g_ticks;
    if (g_profEnable && g_profCount < 4096 && (cs & 0xFFFFu) == dos_irq_code_selector &&
        eip >= 0x1000 && eip < (unsigned int)&code_end)
        g_profSamples[g_profCount++] = eip;
    if (tick_hook) tick_hook();
    if (dos_timer_bios_step(&bios_phase)) {
        ++dos_irq_bios_count;
        return 1; /* Previous handler owns the EOI on chained interrupts. */
    }
    return 0;
}

void dos_irq_keyboard_body(void)
{
    unsigned char status = inportb(0x64);
    if (status & 1) {
        unsigned char byte = inportb(0x60);
        /* AUX bytes and controller parity/timeout errors are not key codes. */
        if (!(status & 0xE0)) {
            dos_keyboard_scancode(byte);
            ++dos_irq_keyboard_count;
        }
    }
    outportb(0x20, 0x20);
}

static void pit_program(unsigned int divisor)
{
    outportb(0x43, 0x36); /* Channel 0, low/high, mode 3, binary. */
    outportb(0x40, divisor & 255);
    outportb(0x40, divisor >> 8);
}

void dos_irq_set_tick_hook(void (*hook)(void))
{
    unsigned long state = dos_keyboard_irq_save();
    tick_hook = hook;
    dos_keyboard_irq_restore(state);
}

void dos_irq_shutdown(void)
{
    unsigned long state = dos_keyboard_irq_save();
    if (installed) {
        outportb(0x21, inportb(0x21) | 3);
        pit_program(0); /* Restore DOS/BIOS standard 18.2065 Hz period. */
        if (__dpmi_set_protected_mode_interrupt_vector(timer_vector, &dos_irq_old_timer) ||
            __dpmi_set_protected_mode_interrupt_vector(keyboard_vector, &old_keyboard))
            abort(); /* Never unlock memory beneath a live vector. */
        installed = 0;
        tick_hook = 0;
        dos_keyboard_reset();
        outportb(0x21, old_pic_mask);
    }
    dos_keyboard_irq_restore(state);
    if (image_locked) {
        __dpmi_unlock_linear_region(&locked_image);
        image_locked = 0;
    }
}

int dos_irq_init(void)
{
    __dpmi_version_ret version;
    __dpmi_paddr timer, keyboard;
    unsigned long state, image_base;
    unsigned int guard;
    if (installed) return -1;
    /* IRQ handlers use CS-relative globals; changing the image base later is
     * forbidden. The default non-moving sbrk policy satisfies this contract. */
    if (_crt0_startup_flags & _CRT0_FLAG_UNIX_SBRK) return -1;
    if (__dpmi_get_version(&version)) return -1;
    timer_vector = version.master_pic;
    keyboard_vector = timer_vector + 1;
    if (__dpmi_get_protected_mode_interrupt_vector(timer_vector, &dos_irq_old_timer) ||
        __dpmi_get_protected_mode_interrupt_vector(keyboard_vector, &old_keyboard)) return -1;
    dos_irq_data_selector = _my_ds();
    dos_irq_code_selector = _my_cs();
    /* Check the lock result (CRT0's LOCK_MEMORY flag does not check it).
     * DJGPP's load image begins at offset 0x1000; end includes all static BSS. */
    if (__dpmi_get_segment_base_address(dos_irq_data_selector, &image_base)) return -1;
    locked_image.address = image_base + 0x1000;
    locked_image.size = (unsigned long)&image_end - 0x1000;
    if (__dpmi_lock_linear_region(&locked_image)) return -1;
    image_locked = 1;
    if (!exit_registered) {
        if (atexit(dos_irq_shutdown)) { dos_irq_shutdown(); return -1; }
        exit_registered = 1;
    }
    timer.offset32 = (unsigned long)dos_irq_timer_entry;
    timer.selector = dos_irq_code_selector;
    keyboard.offset32 = (unsigned long)dos_irq_keyboard_entry;
    keyboard.selector = dos_irq_code_selector;
    state = dos_keyboard_irq_save();
    old_pic_mask = inportb(0x21);
    outportb(0x21, old_pic_mask | 3);
    /* Discard pending pre-game bytes while both handlers are disabled. Never
     * send controller commands or change its translation/mouse configuration. */
    for (guard = 0; guard < 32 && (inportb(0x64) & 1); ++guard) inportb(0x60);
    dos_keyboard_reset();
    g_ticks = g_profCount = dos_irq_keyboard_count = dos_irq_bios_count = bios_phase = 0;
    g_profEnable = 0;
    tick_hook = 0;
    if (__dpmi_set_protected_mode_interrupt_vector(timer_vector, &timer)) goto fail;
    if (__dpmi_set_protected_mode_interrupt_vector(keyboard_vector, &keyboard)) {
        if (__dpmi_set_protected_mode_interrupt_vector(timer_vector, &dos_irq_old_timer)) abort();
        goto fail;
    }
    installed = 1;
    pit_program(DOS_PIT_DIVISOR);
    outportb(0x21, old_pic_mask & ~3);
    dos_keyboard_irq_restore(state);
    return 0;
fail:
    outportb(0x21, old_pic_mask);
    dos_keyboard_irq_restore(state);
    dos_irq_shutdown();
    return -1;
}
