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

/* Commit the initial image/CRT stack before its first real-mode DOS call.
 * The default demand-committed startup exposed an interrupt-frame page-boundary
 * fault in our pinned 86Box interpreter. This is normal DJGPP resident-memory
 * policy, not an emulator speed override or a changed game allocation budget.
 * CRT locking is unchecked: retain the checked ISR-image lock below. Keep
 * non-moving sbrk; IRQ CS-relative references must never be relocated. */
int _crt0_startup_flags = _CRT0_FLAG_NONMOVE_SBRK | _CRT0_FLAG_LOCK_MEMORY;

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
#ifdef DOS_VGA_SCAN_QUAD
/* Raster timing for the scan-quadrupled view (video.c). PIT channel 0 runs
 * one-shots (mode 0) instead of a 100 Hz square wave: each interrupt is the
 * earlier of the next 100 Hz tick and the next raster event, and ticks are
 * produced from the accumulated PIT count, so g_ticks keeps 99.9985 Hz on
 * average. Raster events: shortly before vertical retrace, wait for its
 * start (the frame reference) and select the next frame's scan layout from
 * the latched CRTC start; on quad frames, a second event near scanline 397
 * restores two scanlines per row for the HUD below the line-compare split.
 * Under the BIOS 320x240 timing a scanline is 1193182/31468.75 PIT counts. */
#define PIT_LINES(n) ((unsigned int)(((n) * 37917u + 500u) / 1000u))
#define RASTER_FRAME PIT_LINES(527)       /* Vertical total 0x20d+2: 19982 counts */
#define RASTER_EARLY PIT_LINES(3)         /* sync event before retrace */
#define RASTER_SPLIT PIT_LINES(36 + 397)  /* retrace start (491) -> scanline 397 */
static unsigned int pit_base, pit_count, next_tick_at, next_raster_at, raster_sync_at;
static unsigned int raster_period = RASTER_FRAME;  /* Measured between locked syncs. */
static int raster_split_pending, raster_locked;
volatile int dos_raster_video;   /* Set by video.c only while Mode X is up. */
/* Page flips are posted by video.c and written here at retrace together with
 * the matching scan layout, so a page and its layout always change in the
 * same frame (86Box latches the start address only at the frame's end). */
volatile unsigned int dos_raster_start, dos_raster_start_pending;
volatile unsigned int dos_raster_frames, dos_raster_quad_frames, dos_raster_missed_syncs,
    dos_raster_split_max_line, dos_raster_split_late;
static unsigned int pit_read_elapsed(void)
{
    unsigned int count;
    outportb(0x43, 0x00);              /* Latch channel 0. */
    count = inportb(0x40);
    count |= inportb(0x40) << 8;
    /* Mode 0 keeps counting down through zero: elapsed modulo 65536. */
    return (pit_count - count) & 0xffffu;
}
static unsigned int pit_now(void) { return pit_base + pit_read_elapsed(); }
static void pit_one_shot(unsigned int now, unsigned int delay)
{
    if (delay < 16u) delay = 16u;
    if (delay > 0xffffu) delay = 0xffffu;
    outportb(0x43, 0x30);              /* Channel 0, low/high, mode 0. */
    outportb(0x40, delay & 255);
    outportb(0x40, delay >> 8);
    pit_base = now; pit_count = delay;
}
static void raster_event(void)
{
    unsigned char index = inportb(0x3d4);
    if (!raster_split_pending) {
        unsigned int now = pit_now(), start, window = PIT_LINES(raster_locked ? 8 : 32);
        int quad, found = 0;
        /* Wait a few scanlines for the retrace edge. Already inside it means
         * this interrupt was late; not reaching it means the phase is lost
         * (startup, mode change). Either way, hunt again in short windows. */
        if (!(inportb(0x3da) & 8)) {
            while (!(found = (inportb(0x3da) & 8) != 0) && pit_now() - now < window) {}
        }
        if (!found) {
            if (raster_locked) ++dos_raster_missed_syncs;
            raster_locked = 0;
            if (dos_raster_video && dos_raster_start_pending) {
                /* Unsynchronized: never hold a flip while the phase is lost. */
                outportw(0x3d4, (dos_raster_start & 0xff00) | 0x0c);
                dos_raster_start_pending = 0;
            }
            next_raster_at = pit_now() + PIT_LINES(24);
            outportb(0x3d4, index);
            return;
        }
        {
            unsigned int sync = pit_now(), period = sync - raster_sync_at;
            /* Track the display's own frame length (PIT and dot clocks differ
             * on real boards); ignore gaps after a lost lock. */
            if (raster_locked && period > RASTER_FRAME - PIT_LINES(8) &&
                period < RASTER_FRAME + PIT_LINES(8))
                raster_period = (raster_period * 7u + period) / 8u;
            raster_locked = 1;
            raster_sync_at = sync;
        }
        if (!dos_raster_video) {
            next_raster_at = raster_sync_at + raster_period - RASTER_EARLY;
            outportb(0x3d4, index);
            return;
        }
        if (dos_raster_start_pending) {
            outportw(0x3d4, (dos_raster_start & 0xff00) | 0x0c);
            dos_raster_start_pending = 0;
        }
        outportb(0x3d4, 0x0c);
        start = inportb(0x3d5);
        quad = (start & 0x1f) == 0x0d;  /* Quad page starts 0x0d00/0x2d00/0x4d00. */
        /* Line compare 399 (bit 8 is already set in CRTC 7) splits to the
         * HUD at offset zero; four scanlines per row above it. */
        outportw(0x3d4, quad ? 0x8f18 : 0xff18);
        outportw(0x3d4, quad ? 0x0309 : 0x4109);
        ++dos_raster_frames;
        if (quad) {
            ++dos_raster_quad_frames;
            raster_split_pending = 1;
            next_raster_at = raster_sync_at + RASTER_SPLIT;
        } else {
            next_raster_at = raster_sync_at + raster_period - RASTER_EARLY;
        }
    } else {
        unsigned int line;
        /* Any write while row 99's scan counter is 0-1, or within the HUD's
         * first scanline, is exact; the split resets the counter at 400. */
        outportw(0x3d4, 0x0109);
        line = (pit_now() - raster_sync_at) * 1000u / 37917u;
        line = line >= 36u ? line - 36u : 0;
        if (line > dos_raster_split_max_line) dos_raster_split_max_line = line;
        if (line > 400u) ++dos_raster_split_late;
        raster_split_pending = 0;
        next_raster_at = raster_sync_at + raster_period - RASTER_EARLY;
    }
    outportb(0x3d4, index);
}
#endif
int dos_irq_timer_body(unsigned int eip, unsigned int cs)
{
#ifdef DOS_VGA_SCAN_QUAD
    unsigned int now = pit_now();
    int chained = 0;
    while ((int)(now - next_tick_at) >= 0) {
        next_tick_at += DOS_PIT_DIVISOR;
        ++g_ticks;
        if (g_profEnable && g_profCount < 4096 && (cs & 0xFFFFu) == dos_irq_code_selector &&
            eip >= 0x1000 && eip < (unsigned int)&code_end)
            g_profSamples[g_profCount++] = eip;
        if (tick_hook) tick_hook();
        if (dos_timer_bios_step(&bios_phase)) {
            ++dos_irq_bios_count;
            chained = 1;
        }
    }
    if ((int)(now - next_raster_at) >= 0) raster_event();
    now = pit_now();
    pit_one_shot(now, ((int)(next_raster_at - next_tick_at) < 0 ? next_raster_at : next_tick_at) - now);
    return chained; /* Previous handler owns the EOI on chained interrupts. */
#else
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
#endif
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
#ifdef DOS_VGA_SCAN_QUAD
    pit_base = 0; pit_count = 0;
    next_tick_at = DOS_PIT_DIVISOR;
    next_raster_at = RASTER_FRAME;   /* First sync finds the retrace itself. */
    raster_sync_at = 0; raster_split_pending = 0;
    pit_one_shot(0, DOS_PIT_DIVISOR);
#else
    pit_program(DOS_PIT_DIVISOR);
#endif
    outportb(0x21, old_pic_mask & ~3);
    dos_keyboard_irq_restore(state);
    return 0;
fail:
    outportb(0x21, old_pic_mask);
    dos_keyboard_irq_restore(state);
    dos_irq_shutdown();
    return -1;
}
