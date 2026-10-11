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
#include "bench_transport.h"
#ifdef DOS_VGA_PROVEN_ROWS
#if !defined(DOS_VGA_BROADCAST) || !defined(DOS_VGA_PACK_ALL)
#error Proven rows require accepted all-plane packing and broadcast
#endif
#include "vga_proven.h"
static unsigned char low_row_proven[200];
static int low_rows_active;
void dos_video_prove_low_rows(unsigned int begin, unsigned int end)
{
    unsigned int y;
    if (begin>end || end>200) return;
    for (y=begin; y<end; ++y) low_row_proven[y]=1;
    low_rows_active=1;
}
void dos_video_unprove_low_rows(unsigned int begin, unsigned int end)
{
    unsigned int y;
    if (begin>end || end>200) return;
    for (y=begin; y<end; ++y) {
        low_row_proven[y]=0;
#ifdef DOS_VGA_PROVEN_VERTICAL
        if (!(y&1u) && y+1<200) low_row_proven[y+1]&=(unsigned char)~2u;
#endif
    }
}
#ifdef DOS_VGA_PROVEN_VERTICAL
void dos_video_prove_low_row_pairs(unsigned int begin, unsigned int end)
{
    unsigned int y;
    if (begin>end || end>200) return;
    for (y=(begin+1u)|1u; y<end; y+=2)
        if (low_row_proven[y-1] && low_row_proven[y]) low_row_proven[y]|=2u;
}
#endif
#endif
#ifdef DOS_VGA_PACK_WORDS
#ifndef DOS_VGA_PACK_ALL
#error Word packing requires the all-plane framebuffer layout
#endif
#include "vga_pack_words.h"
#endif
#ifdef DOS_VGA_BROADCAST
#if !defined(DOS_VGA_PACK_ALL) || !defined(DOS_VGA_HUD_CACHE)
#error Broadcast requires all-plane packing and exact HUD caching
#endif
#include "vga_upload_plan.h"
static struct DosVgaRun upload_runs[DOS_VGA_UPLOAD_MAX_RUNS];
static int repeated_pixel_hint=1;
#endif
void dos_video_set_repeated_pixel_hint(int enabled)
{
#ifdef DOS_VGA_BROADCAST
    repeated_pixel_hint=enabled!=0;
#else
    (void)enabled;
#endif
}
#ifdef DOS_VGA_HUD_CACHE
#include "vga_hud_cache.h"
static unsigned char hud_shadow[3][4][DOS_VGA_HUD_CACHE_MAX_BYTES];
#endif
#ifdef DOS_VGA_FUSED
#if !defined(DOS_VGA_BROADCAST) || !defined(DOS_VGA_PACK_ALL)
#error Fused packing requires accepted all-plane broadcast transport
#endif
#include "vga_fused.h"
#endif
#if defined(DOS_VGA_FUSED_TRANSPOSE) && !defined(DOS_VGA_FUSED)
#error Fused transpose requires fused packing
#endif
#ifdef DOS_ET4000_CHUNKY
#if !defined(DOS_VGA_PACK_ALL) || !defined(DOS_VGA_BROADCAST)
#error ET4000 experiment requires the accepted all-plane/HUD/broadcast path
#endif
/* Opt-in CPU-access experiment, not a different display mode. ET4000 chain-4
 * plus 3CD banks maps consecutive bytes onto the same four display planes.
 * Display timing/start addresses remain the original 320x240 Mode X values.
 * Source: Tseng ET4000 data book, sections 3.2, 4.2 (3CD), 4.4 (TS4).
 * Restore bank zero/unchained access before any normal path or readback. */
static unsigned char chunky_hud[3][40*320];
static unsigned char planar_hud_valid[3],chunky_hud_valid[3];
#ifndef DOS_ET4000_STRIDED
static void et4000_upload(const unsigned char *data,unsigned int bytes,unsigned int address)
{
    outportw(0x3c4,0x0e04); /* CPU chain-4; odd/even remains disabled. */
    outportw(0x3c4,0x0f02);
    while(bytes) {
        unsigned int offset=address&65535u;
        unsigned int count=65536u-offset;
        if(count>bytes) count=bytes;
        outportb(0x3cd,(address>>16)&15u);
        dosmemput(data,count,0xa0000+offset);
        data+=count; address+=count; bytes-=count;
    }
    outportb(0x3cd,0);
    outportw(0x3c4,0x0604);
}
#else
/* Optional direct row upload: avoid gathering 76,800 bytes in system RAM.
 * CPU chain-4 state is set once; bank switches happen only at a crossing,
 * including crossings INSIDE a row. Pitched source padding is never copied. */
static void et4000_upload_rows(const unsigned char *data,unsigned int pitch,
                              unsigned int rows,unsigned int address)
{
    unsigned int row,current_bank=~0u;
    outportw(0x3c4,0x0e04);
    outportw(0x3c4,0x0f02);
    for(row=0;row<rows;++row) {
        const unsigned char *source=data+(size_t)row*pitch;
        unsigned int bytes=320;
        while(bytes) {
            unsigned int offset=address&65535u,count=65536u-offset;
            unsigned int next_bank=(address>>16)&15u;
            if(count>bytes) count=bytes;
            if(next_bank!=current_bank) { outportb(0x3cd,next_bank); current_bank=next_bank; }
            dosmemput(source,count,0xa0000+offset);
            source+=count;address+=count;bytes-=count;
        }
    }
    outportb(0x3cd,0);
    outportw(0x3c4,0x0604);
}
#endif
#endif

#if defined(DOS_ET4000_STRIDED) && !defined(DOS_ET4000_CHUNKY)
#error Direct pitched upload requires the ET4000 chunky path
#endif
#if defined(DOS_ET4000_HUD_WORDS) && (!defined(DOS_ET4000_CHUNKY) || !defined(DOS_VGA_HUD_CACHE))
#error Word-wise chunky HUD checks require the banked path and accepted HUD helper
#endif

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
#ifdef DOS_VGA_PACK_ALL
static unsigned char packed[4 * DOS_VGA_PLANE_BYTES];
#else
static unsigned char packed[DOS_VGA_PLANE_BYTES];
#endif
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
#ifdef DOS_ET4000_CHUNKY
    outportb(0x3d4,0x36);
    if(inportb(0x3d5)&0x10) {
        dos_video_restore(); fputs("ET4000 banked memory unavailable\n",stderr); exit(1);
    }
    outportb(0x3cd,0);
    memset(chunky_hud,0,sizeof(chunky_hud));
    memset(planar_hud_valid,1,sizeof(planar_hud_valid));
    memset(chunky_hud_valid,1,sizeof(chunky_hud_valid));
#endif
    memset(packed, 0, sizeof(packed));
#ifdef DOS_VGA_HUD_CACHE
    /* The following all-plane clear establishes the same known zero content
     * in each VGA page; never confuse a RAM page's HUD cache with VRAM state. */
    memset(hud_shadow,0,sizeof(hud_shadow));
#endif
    outportw(0x3c4, 0x0f02);
    for (i = 0; i < VGA_PAGES; ++i)
        dosmemput(packed, DOS_VGA_PLANE_BYTES, 0xa0000 + i * DOS_VGA_PLANE_BYTES);
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
#ifdef DOS_VGA_FUSED
    int fused_count=-1;
#endif
#ifdef DOS_VGA_PROVEN_ROWS
    int proven_count=-1;
    int use_proven=low_rows_active && repeated_pixel_hint;
    low_rows_active=0;
#endif
#ifdef DOSCRAFT_BENCH
    unsigned int stamp;
#endif
#ifdef DOS_ET4000_CHUNKY
    /* Low/Medium keep repeated-pixel broadcast. High uses row-gathered bytes,
     * not four-way deinterleaving. All 240 rows, three pages and HUD identity
     * survive transitions between access paths. No visible-page upload. */
    if(!repeated_pixel_hint) {
        unsigned int y,bytes=320*200;
        int changed;
        if(!source || pitch<320) {
            dos_video_restore(); fputs("Invalid 320x240 framebuffer\n",stderr); exit(1);
        }
#ifdef DOSCRAFT_BENCH
        stamp=bench_us();
#endif
#ifndef DOS_ET4000_STRIDED
        for(y=0;y<240;++y) memcpy(packed+y*320,source+(size_t)y*pitch,320);
#ifndef DOS_ET4000_HUD_WORDS
        changed=!chunky_hud_valid[next_page] ||
            memcmp(packed+200*320,chunky_hud[next_page],40*320)!=0;
        if(changed) {
            memcpy(chunky_hud[next_page],packed+200*320,40*320);
            bytes=320*240;
        }
#else
        changed=!chunky_hud_valid[next_page];
        for(y=0;y<4;++y)
            changed|=dos_vga_hud_changed(packed+200*320+y*3200,chunky_hud[next_page]+y*3200,3200);
        if(changed) bytes=320*240;
#endif
#else
        changed=!chunky_hud_valid[next_page];
#ifndef DOS_ET4000_HUD_WORDS
        for(y=0;!changed && y<40;++y)
            changed=memcmp(source+(size_t)(y+200)*pitch,chunky_hud[next_page]+y*320,320)!=0;
        if(changed) {
            for(y=0;y<40;++y) memcpy(chunky_hud[next_page]+y*320,source+(size_t)(y+200)*pitch,320);
            bytes=320*240;
        }
#else
        for(y=0;y<40;++y)
            changed|=dos_vga_hud_changed(source+(size_t)(y+200)*pitch,chunky_hud[next_page]+y*320,320);
        if(changed) bytes=320*240;
#endif
#endif
#ifdef DOSCRAFT_BENCH
        dos_bench_pack+=bench_us()-stamp;
        stamp=bench_us();
#endif
#ifndef DOS_ET4000_STRIDED
        et4000_upload(packed,bytes,offset*4);
#else
        et4000_upload_rows(source,pitch,bytes/320,offset*4);
#endif
        chunky_hud_valid[next_page]=1;
        planar_hud_valid[next_page]=0;
#ifdef DOSCRAFT_BENCH
        dos_bench_upload+=bench_us()-stamp;
#endif
        goto page_flip;
    }
    chunky_hud_valid[next_page]=0;
#endif
#ifdef DOS_VGA_PACK_ALL
#ifdef DOSCRAFT_BENCH
    stamp=bench_us();
#endif
#ifdef DOS_VGA_FUSED
    if(repeated_pixel_hint) {
        fused_count=dos_vga_pack_fused(source,pitch,packed,sizeof(packed),
                                      upload_runs,DOS_VGA_UPLOAD_MAX_RUNS);
        if(fused_count<0) {
            dos_video_restore(); fputs("Invalid fused framebuffer\n",stderr); exit(1);
        }
    } else
#endif
#ifdef DOS_VGA_PROVEN_ROWS
    if(use_proven) {
        proven_count=dos_vga_proven_pack_plan(source,pitch,packed,sizeof(packed),
                                               low_row_proven,upload_runs,DOS_VGA_UPLOAD_MAX_RUNS);
        if(proven_count<0) {
            dos_video_restore(); fputs("Invalid proven framebuffer\n",stderr); exit(1);
        }
    } else
#endif
    if(
#ifdef DOS_VGA_PACK_WORDS
       dos_vga_pack_words(source,pitch,packed,sizeof(packed))
#else
       dos_vga_pack_all(source,pitch,packed,sizeof(packed))
#endif
       ) {
        dos_video_restore(); fputs("Invalid 320x240 framebuffer\n",stderr); exit(1);
    }
#ifdef DOSCRAFT_BENCH
    dos_bench_pack+=bench_us()-stamp;
#endif
#endif
#ifdef DOS_VGA_BROADCAST
    if(repeated_pixel_hint) {
        int count,r;
#ifdef DOSCRAFT_BENCH
        stamp=bench_us();
#endif
#ifdef DOS_VGA_FUSED
        count=fused_count;
#elif defined(DOS_VGA_PROVEN_ROWS)
        count=use_proven ? proven_count :
            dos_vga_upload_plan(packed,DOS_VGA_PLANE_BYTES,200,upload_runs,DOS_VGA_UPLOAD_MAX_RUNS);
#else
        count=dos_vga_upload_plan(packed,DOS_VGA_PLANE_BYTES,200,upload_runs,DOS_VGA_UPLOAD_MAX_RUNS);
#endif
        if(count<0) { dos_video_restore(); fputs("Invalid VGA upload plan\n",stderr); exit(1); }
        for(r=0;r<count;++r) {
            const struct DosVgaRun *run=&upload_runs[r];
            unsigned int row_offset=run->row*80u;
            outportw(0x3c4,(run->mask<<8)|2);
            dosmemput(packed+run->plane*DOS_VGA_PLANE_BYTES+row_offset,
                      run->rows*80u,0xa0000+offset+row_offset);
        }
        for(plane=0;plane<4;++plane) {
            const unsigned char *hud=packed+plane*DOS_VGA_PLANE_BYTES+200*80;
            if(dos_vga_hud_changed(hud,hud_shadow[next_page][plane],DOS_VGA_HUD_CACHE_MAX_BYTES)
#ifdef DOS_ET4000_CHUNKY
               || !planar_hud_valid[next_page]
#endif
               ) {
                outportw(0x3c4,((1u<<plane)<<8)|2);
                dosmemput(hud,DOS_VGA_HUD_CACHE_MAX_BYTES,0xa0000+offset+200*80);
            }
        }
#ifdef DOSCRAFT_BENCH
        dos_bench_upload+=bench_us()-stamp;
#endif
    } else
#endif
    for (plane = 0; plane < 4; ++plane) {
        const unsigned char *plane_data;
        unsigned int upload_bytes=DOS_VGA_PLANE_BYTES;
#ifndef DOS_VGA_PACK_ALL
#ifdef DOSCRAFT_BENCH
        stamp=bench_us();
#endif
        if (dos_vga_pack_plane(source, pitch, packed, sizeof(packed), plane)) {
            dos_video_restore();
            fputs("Invalid 320x240 framebuffer\n", stderr);
            exit(1);
        }
#ifdef DOSCRAFT_BENCH
        dos_bench_pack+=bench_us()-stamp;
#endif
#endif
#ifdef DOSCRAFT_BENCH
        stamp=bench_us();
#endif
        plane_data=packed
#ifdef DOS_VGA_PACK_ALL
            +plane*DOS_VGA_PLANE_BYTES
#endif
            ;
#ifdef DOS_VGA_HUD_CACHE
        /* Exact bytes, destination-page keyed. Works for arbitrary title,
         * loading, menu/dimming, HUD changes and source pitch, not just play. */
        if(dos_vga_hud_changed(plane_data+200*80,hud_shadow[next_page][plane],
                              DOS_VGA_HUD_CACHE_MAX_BYTES)==0
#ifdef DOS_ET4000_CHUNKY
           && planar_hud_valid[next_page]
#endif
           )
            upload_bytes=200*80;
#endif
        outportw(0x3c4, ((1 << plane) << 8) | 2);
        dosmemput(plane_data,upload_bytes,0xa0000 + offset);
#ifdef DOSCRAFT_BENCH
        dos_bench_upload+=bench_us()-stamp;
#endif
    }
#ifdef DOS_ET4000_CHUNKY
    planar_hud_valid[next_page]=1;
page_flip:
#endif
    outportw(0x3c4, 0x0f02);
    /* Preserve hidden-page presentation, not four planes fading onto the
     * visible page. Mode X CRTC start addresses are bytes per plane. Leave
     * the current retrace before programming the next start, then wait for
     * the retrace that latches it. Both waits are bounded on broken hardware. */
    start = wait_clock();
#ifdef DOSCRAFT_BENCH
    stamp=bench_us();
#endif
    while (video_in_vsync() && wait_pending(start)) {}
    outportw(0x3d4, (offset & 0xff00) | 0x0c);
    outportw(0x3d4, ((offset & 0xff) << 8) | 0x0d);
    while (!video_in_vsync() && wait_pending(start)) {}
#ifdef DOSCRAFT_BENCH
    dos_bench_wait+=bench_us()-stamp;
    if(!wait_pending(start)) ++dos_bench_timeouts;
#endif
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
        if (memcmp(packed, readback, DOS_VGA_PLANE_BYTES)) return -1;
    }
    outportw(0x3ce, 0x0004);
    return 0;
}
