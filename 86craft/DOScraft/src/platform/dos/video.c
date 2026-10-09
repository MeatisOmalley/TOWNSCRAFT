/* Direct display transport, NOT a renderer optimization. The game's 320x240
 * indexed image (including the 40-row HUD) remains unchanged. BIOS mode 13h
 * initializes VGA; standard unchained 320x240 timing then exposes four planes.
 * Register reference: Abrash, Graphics Programming Black Book, ch.47,
 * https://www.phatcode.net/res/224/files/html/ch47/47-02.html
 */
#include <dpmi.h>
#include <pc.h>
#include <sys/movedata.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "vga_pack.h"
#include "video_backend.h"

static int old_mode = -1;
static unsigned int shown_page;
static volatile unsigned int *irq_clock;
static unsigned int irq_rate;
void dos_video_set_tick_clock(volatile unsigned int *ticks, unsigned int rate)
{
    irq_clock = rate >= 10 ? ticks : 0;
    irq_rate = rate;
}
static uclock_t wait_clock(void)
{
    return irq_clock ? (uclock_t)*irq_clock : uclock();
}
static int wait_pending(uclock_t start)
{
    /* g_ticks wraps at 32 bits; uclock_t is wider. Subtract at the correct
     * width or a wrap could turn the bounded wait into a permanent loop. */
    return irq_clock ? (unsigned int)(*irq_clock - (unsigned int)start) < irq_rate / 10 :
                       uclock() - start < UCLOCKS_PER_SEC / 10;
}
#define VGA_PAGES 3
typedef char pages_fit_vga_window[(VGA_PAGES * DOS_VGA_PLANE_BYTES <= 65536) ? 1 : -1];
static unsigned char packed[DOS_VGA_PLANE_BYTES];
static unsigned char readback[DOS_VGA_PLANE_BYTES];

static void bios_mode(int number)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = number;
    __dpmi_int(0x10, &r);
}

void dos_video_restore(void)
{
    if (old_mode >= 0) {
        bios_mode(old_mode);
        old_mode = -1;
    }
}

void video_init(void)
{
    /* Index/data words. Horizontal timing stays at the BIOS 320-pixel mode. */
    static const unsigned short vertical[] = {
        0x0d06, 0x3e07, 0x4109, 0xea10, 0xac11,
        0xdf12, 0x0014, 0xe715, 0x0616, 0xe317
    };
    __dpmi_regs r;
    unsigned int i;
    if (old_mode >= 0) return;
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x0f;
    __dpmi_int(0x10, &r);
    old_mode = r.h.al;
    shown_page = 0;
    atexit(dos_video_restore);
    bios_mode(0x13);
    outportw(0x3c4, 0x0604); /* No chain-4, extended memory. */
    outportw(0x3c4, 0x0100); /* Synchronous reset while changing clock. */
    outportb(0x3c2, 0xe3);
    outportw(0x3c4, 0x0300);
    outportb(0x3d4, 0x11);
    outportb(0x3d5, inportb(0x3d5) & 0x7f);
    for (i = 0; i < sizeof(vertical) / sizeof(vertical[0]); ++i)
        outportw(0x3d4, vertical[i]);
    outportw(0x3d4, 0x000c); /* Display page zero. */
    outportw(0x3d4, 0x000d);
    outportw(0x3ce, 0x4005); /* 256-color shift, write/read mode zero. */
    outportw(0x3ce, 0x0506); /* A0000 mapping, graphics, no odd/even. */
    outportw(0x3ce, 0xff08); /* All bits writable. */
    memset(packed, 0, sizeof(packed));
    outportw(0x3c4, 0x0f02);
    for (i = 0; i < VGA_PAGES; ++i)
        dosmemput(packed, sizeof(packed), 0xa0000 + i * DOS_VGA_PLANE_BYTES);
}

void video_set_palette(int index, int red, int green, int blue)
{
    if (index < 0 || index > 255) return;
    outportb(0x3c8, index);
    outportb(0x3c9, red >> 2);
    outportb(0x3c9, green >> 2);
    outportb(0x3c9, blue >> 2);
}

int video_in_vsync(void)
{
    return (inportb(0x3da) & 8) != 0;
}

void video_wait_vsync(void)
{
    /* A broken display must not trap the program forever. */
    uclock_t start = wait_clock();
    while (video_in_vsync() && wait_pending(start)) {}
    while (!video_in_vsync() && wait_pending(start)) {}
}

void dos_video_present(const unsigned char *source, unsigned int pitch)
{
    unsigned int plane;
    unsigned int next_page = (shown_page + 1) % VGA_PAGES;
    unsigned int offset = next_page * DOS_VGA_PLANE_BYTES;
    uclock_t start;
    for (plane = 0; plane < 4; ++plane) {
        if (dos_vga_pack_plane(source, pitch, packed, sizeof(packed), plane)) {
            dos_video_restore();
            fputs("Invalid 320x240 framebuffer\n", stderr);
            exit(1);
        }
        outportw(0x3c4, ((1 << plane) << 8) | 2);
        dosmemput(packed, sizeof(packed), 0xa0000 + offset);
    }
    outportw(0x3c4, 0x0f02);
    /* Preserve hidden-page presentation, not four planes fading onto the
     * visible page. Mode X CRTC start addresses are bytes per plane. Leave
     * the current retrace before programming the next start, then wait for
     * the retrace that latches it. Both waits are bounded on broken hardware. */
    start = wait_clock();
    while (video_in_vsync() && wait_pending(start)) {}
    outportw(0x3d4, (offset & 0xff00) | 0x0c);
    outportw(0x3d4, ((offset & 0xff) << 8) | 0x0d);
    while (!video_in_vsync() && wait_pending(start)) {}
    shown_page = next_page;
}

int dos_video_verify(const unsigned char *source, unsigned int pitch)
{
    unsigned int plane;
    unsigned int offset = shown_page * DOS_VGA_PLANE_BYTES;
    unsigned int display_start;
    outportb(0x3d4, 0x0c);
    display_start = inportb(0x3d5) << 8;
    outportb(0x3d4, 0x0d);
    display_start |= inportb(0x3d5);
    if (display_start != offset) return -1;
    for (plane = 0; plane < 4; ++plane) {
        if (dos_vga_pack_plane(source, pitch, packed, sizeof(packed), plane)) return -1;
        outportw(0x3ce, (plane << 8) | 4);
        dosmemget(0xa0000 + offset, sizeof(readback), readback);
        if (memcmp(packed, readback, sizeof(packed))) return -1;
    }
    outportw(0x3ce, 0x0004);
    return 0;
}
