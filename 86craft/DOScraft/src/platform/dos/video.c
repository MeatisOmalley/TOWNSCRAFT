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
#ifdef DOS_RASTER_FINISH_SKIP
static int last_proven_present;
#endif
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
#if defined(DOS_VGA_HUD_PROOF) && (!defined(DOS_VGA_PROVEN_ROWS) || !defined(DOS_VGA_BROADCAST) || !defined(DOS_ET4000_CHUNKY))
#error HUD proof requires proven rows, broadcast, and ET4000 page-path tracking
#endif
static int hud_proof_page=-1;
void dos_video_prove_hud_unchanged(int page)
{
#ifdef DOS_VGA_HUD_PROOF
    hud_proof_page=page;
#else
    (void)page;
#endif
}
void dos_video_unprove_hud(void) { hud_proof_page=-1; }
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
#ifdef DOS_VERY_LOW
extern int dos_very_low_mode;
static int last_very_low_present;
#endif
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
#ifdef DOS_ET4000_STRIDED
/* gfx_present rotates three RAM pages. The previously presented RAM page
 * remains untouched while the next one is drawn, so it is an exact shadow
 * of the single displayed inventory page without another 75 KiB buffer. */
static const unsigned char *inventory_shadow;
static unsigned int inventory_shadow_pitch;
static unsigned int inventory_shadow_page;
static int inventory_shadow_valid;
#endif
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
/* The menu's world is frozen. Compare against the actual displayed page and
 * write only changed scanlines (usually the old/new pointer or a slot). */
static void et4000_upload_inventory(const unsigned char *data,unsigned int pitch,
                                    unsigned int address)
{
    unsigned int row,current_bank=~0u;
    int writing=0;
    for(row=0;row<240;++row) {
        const unsigned char *source=data+(size_t)row*pitch;
        const unsigned char *shadow=inventory_shadow+(size_t)row*inventory_shadow_pitch;
        unsigned int at=address+row*320,bytes=320;
        if(!memcmp(source,shadow,320)) continue;
        if(!writing) {
            outportw(0x3c4,0x0e04);
            outportw(0x3c4,0x0f02);
            writing=1;
        }
        while(bytes) {
            unsigned int offset=at&65535u,count=65536u-offset;
            unsigned int next_bank=(at>>16)&15u;
            if(count>bytes) count=bytes;
            if(next_bank!=current_bank) {
                outportb(0x3cd,next_bank); current_bank=next_bank;
            }
            dosmemput(source,count,0xa0000+offset);
            source+=count; at+=count; bytes-=count;
        }
    }
    if(writing) {
        outportb(0x3cd,0);
        outportw(0x3c4,0x0604);
    }
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
static int inventory_hint;
void dos_video_set_inventory_hint(void) { inventory_hint=1; }
static volatile unsigned int *irq_clock;
static unsigned int irq_rate;
#ifdef DOS_VGA_ASYNC_FLIP
static uclock_t last_flip_at, prev_flip_at;
static unsigned int flip_count;
static int latch_pending;
/* Sub-tick page-reuse guard. A 100 Hz tick alone cannot tell 17 ms from
 * 29 ms, so moving frames waited for a retrace they did not need. PIT
 * channel 2 (gate on, speaker output off) runs mode 2 modulo 65536 counts
 * (54.9 ms); the benchmark's bench_us() owns the identical setup. SB/OPL
 * audio does not use channel 2; PC-speaker output would need another clock. */
#define FLIP_SETTLED_COUNTS 20881u /* 17.5 ms: one 59.94 Hz refresh + margin */
static unsigned int flip_tick, flip_count2, prev_flip_tick, prev_flip_count2, flip_settled_ticks;
static int fine_guard, flip_fine_valid, pit2_on;
#ifndef DOSCRAFT_BENCH
static int saved_61;
#endif
static unsigned int pit2_count(void)
{
    unsigned int count;
    if(!pit2_on) {
#ifdef DOSCRAFT_BENCH
        (void)bench_us();
#else
        saved_61=inportb(0x61); outportb(0x61,(saved_61|1)&~2);
        outportb(0x43,0xb4); outportb(0x42,0); outportb(0x42,0);
#endif
        pit2_on=1;
    }
    outportb(0x43,0x80);
    count=inportb(0x42);
    count|=inportb(0x42)<<8;
    return count;
}
static void pit2_snapshot(unsigned int *ticks,unsigned int *count)
{
    do { *ticks=*irq_clock; *count=pit2_count(); } while(*ticks!=*irq_clock);
}
#endif
void dos_video_set_tick_clock(volatile unsigned int *ticks, unsigned int rate)
{
    irq_clock = rate >= 10 ? ticks : 0;
    irq_rate = rate;
#ifdef DOS_VGA_ASYNC_FLIP
    /* Fewer than flip_settled_ticks tick edges may span up to that many tick
     * periods; the fine count is exact only while that stays under 54.9 ms.
     * At least flip_settled_ticks edges always span >= 17.5 ms. */
    flip_settled_ticks=(rate*35u+1999u)/2000u+1u;
    fine_guard=irq_clock && flip_settled_ticks*10000u<rate*549u;
    flip_fine_valid=0;
#endif
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
#ifdef DOS_VGA_ASYNC_FLIP
/* The page about to be written was last shown before the SECOND most recent
 * flip; the display leaves it at the first retrace after that flip (latching
 * either start). Only two flips within one refresh can require a wait. */
static int flip_may_be_pending(void)
{
    uclock_t now=wait_clock();
    if(fine_guard && flip_fine_valid>=2) {
        unsigned int ticks,count;
        pit2_snapshot(&ticks,&count);
        if(ticks-prev_flip_tick>=flip_settled_ticks) return 0;
        return ((prev_flip_count2-count)&65535u)<FLIP_SETTLED_COUNTS;
    }
    /* Crossing three 100 Hz ticks guarantees at least 20 ms elapsed, longer
     * than one 60 Hz display refresh. On a faster clock use the same margin. */
    return irq_clock ? (unsigned int)(now-(unsigned int)prev_flip_at)<=irq_rate/60+1 :
                       now-prev_flip_at<UCLOCKS_PER_SEC/50;
}
#endif
#define VGA_PAGES 3
typedef char pages_fit_vga_window[(VGA_PAGES * DOS_VGA_PLANE_BYTES <= 65536) ? 1 : -1];
#ifdef DOS_VGA_PACK_ALL
static unsigned char packed[4 * DOS_VGA_PLANE_BYTES];
#else
static unsigned char packed[DOS_VGA_PLANE_BYTES];
#endif
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED) && defined(DOS_VGA_PACK_ALL)
#define INVENTORY_X 58u
#define INVENTORY_Y 8u
#define INVENTORY_W 204u
#define INVENTORY_H 224u
static int inventory_canvas_valid;
#endif
int dos_video_inventory_canvas_save(const unsigned char *source, unsigned int pitch)
{
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED) && defined(DOS_VGA_PACK_ALL)
    unsigned int y;
    if(!source || pitch<320) return 0;
    for(y=0;y<INVENTORY_H;++y)
        memcpy(packed+y*INVENTORY_W,
               source+(size_t)(y+INVENTORY_Y)*pitch+INVENTORY_X,INVENTORY_W);
    inventory_canvas_valid=1;
    return 1;
#else
    (void)source; (void)pitch;
    return 0;
#endif
}
int dos_video_inventory_canvas_restore(unsigned char *target, unsigned int pitch)
{
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED) && defined(DOS_VGA_PACK_ALL)
    unsigned int y;
    if(!inventory_canvas_valid || !target || pitch<320) return 0;
    for(y=0;y<INVENTORY_H;++y)
        memcpy(target+(size_t)(y+INVENTORY_Y)*pitch+INVENTORY_X,
               packed+y*INVENTORY_W,INVENTORY_W);
    return 1;
#else
    (void)target; (void)pitch;
    return 0;
#endif
}
static unsigned char readback[DOS_VGA_PLANE_BYTES];
#ifdef DOS_LOW_PLANAR
#if !defined(DOS_VGA_PROVEN_ROWS) || !defined(DOS_VGA_PROVEN_VERTICAL)
#error Planar Low requires the producer-proven vertical presenter
#endif
/* The scale-2 renderer writes planar view rows straight into packed planes
 * 0 and 2 (low_planar.S, PLANE_C=38400). planar_rows marks, per logical row,
 * which view rows the packed planes already hold for this frame. */
typedef char planar_plane_offset[(2*DOS_VGA_PLANE_BYTES==38400) ? 1 : -1];
static const unsigned char *planar_rows;
static const unsigned char no_row_proof[200];
unsigned char *dos_video_planar_target(void) { return packed; }
void dos_video_planar_frame(const unsigned char *rows) { planar_rows=rows; }
#endif

#ifdef DOS_VGA_SCAN_QUAD
#if !defined(DOS_LOW_PLANAR) || !defined(DOS_VGA_ASYNC_FLIP) || !defined(DOS_ET4000_CHUNKY)
#error Scan quadrupling extends the planar, async-flip ET4000 presenter
#endif
/* Prototype: scale-2 play frames whose view rows are all producer-proven
 * pairs (the crosshair band excepted, whose even rows hold it) store the
 * 100 logical rows once per page. The CRTC shows each four scanlines; a
 * line-compare split at scanline 400 restarts at offset zero, where a single
 * shared copy of the 40-row HUD lives (irq.c times the scan-count changes).
 * Rows 100-101 repeat row 99 so an early boundary write stays invisible.
 * Quad layout: HUD 0..3199, pages 0x0d00/0x2d00/0x4d00 (8160 bytes each),
 * all below normal page 2 (38400), which is the only page used to change
 * layouts in either direction. */
#define QUAD_PAGE(i) (0x0d00u + (i) * 0x2000u)
#define QUAD_ROWS 102u
typedef char quad_layout_fits[(QUAD_PAGE(2) + QUAD_ROWS * 80u <= 2u * DOS_VGA_PLANE_BYTES) ? 1 : -1];
extern volatile int dos_raster_video;
extern volatile unsigned int dos_raster_start, dos_raster_start_pending;
static int quad_shown, quad_hud_valid;
static unsigned int quad_page;
static unsigned char quad_hud_shadow[4][DOS_VGA_HUD_CACHE_MAX_BYTES];
static unsigned char quad_gather[QUAD_ROWS*80u];
unsigned int dos_quad_frames, dos_quad_transitions;
/* irq.c writes the posted start address at the next retrace. */
static void quad_post_flip(unsigned int offset)
{
    dos_raster_start=offset;
    dos_raster_start_pending=1;
}
static void quad_wait_flip(void)
{
    uclock_t start=wait_clock();
    while(dos_raster_start_pending && wait_pending(start)) {}
}
static int quad_rows_doubled(void)
{
    unsigned int y;
    for (y=1; y<200; y+=2)
        if ((low_row_proven[y]&3u)!=3u && (y<96 || y>105)) return 0; /* Crosshair pairs. */
    return 1;
}
#endif

static void bios_mode(int number)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = number;
    __dpmi_int(0x10, &r);
}

void dos_video_restore(void)
{
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED) && defined(DOS_VGA_PACK_ALL)
    inventory_canvas_valid=0;
#endif
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED)
    inventory_shadow_valid=0;
#endif
    if (old_mode >= 0) {
#ifdef DOS_VGA_SCAN_QUAD
        dos_raster_video=0;
        quad_shown=0;
#endif
        bios_mode(old_mode);
        old_mode = -1;
        hud_proof_page=-1;
#ifdef DOS_VGA_ASYNC_FLIP
        flip_count=0;
        latch_pending=0;
#ifndef DOSCRAFT_BENCH
        if(pit2_on) { outportb(0x61,saved_61); pit2_on=0; }
#endif
#endif
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
    inventory_hint=0;
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED) && defined(DOS_VGA_PACK_ALL)
    inventory_canvas_valid=0;
#endif
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED)
    inventory_shadow_valid=0;
#endif
    hud_proof_page=-1;
#ifdef DOS_VGA_ASYNC_FLIP
    flip_count=0;
    latch_pending=0;
#endif
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
#ifdef DOS_VGA_SCAN_QUAD
    quad_shown=0; quad_hud_valid=0; quad_page=0;
    dos_raster_video=1;
#endif
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
    int inventory=inventory_hint;
    unsigned int plane;
    unsigned int next_page = (shown_page + 1) % VGA_PAGES;
    unsigned int offset = next_page * DOS_VGA_PLANE_BYTES;
#ifdef DOSCRAFT_BENCH
    unsigned int stamp;
#endif
    inventory_hint=0;
#if !defined(DOS_VGA_BROADCAST) && !(defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED))
    (void)inventory;
#endif
#ifdef DOS_VGA_BROADCAST
    /* The grid and pointer invalidate scale-2 row/texel proofs. */
    if(inventory) repeated_pixel_hint=0;
#endif
#ifndef DOS_VGA_ASYNC_FLIP
    uclock_t start;
#endif
#ifdef DOS_VGA_FUSED
    int fused_count=-1;
#endif
#ifdef DOS_VGA_PROVEN_ROWS
    int proven_count=-1;
    int use_proven=low_rows_active && repeated_pixel_hint;
#ifdef DOS_LOW_PLANAR
    /* Planar frames always come from the scale-2 renderer (repeated pixels). */
    const unsigned char *planar=repeated_pixel_hint ? planar_rows : 0;
    int use_pack_plan=use_proven || planar;
    planar_rows=0;
#else
    int use_pack_plan=use_proven;
#endif
    low_rows_active=0;
#ifdef DOS_RASTER_FINISH_SKIP
    last_proven_present=use_proven;
#endif
#endif
#ifdef DOS_VERY_LOW
    last_very_low_present=use_proven && dos_very_low_mode;
#endif
#ifdef DOS_VGA_HUD_PROOF
    int skip_hud=use_proven && hud_proof_page==(int)next_page && planar_hud_valid[next_page];
#endif
    hud_proof_page=-1;
#ifdef DOS_VGA_SCAN_QUAD
    /* Layout changes go through normal page 2, which overlaps nothing in the
     * quad layout; wait for it to latch before the other layout's pages are
     * reused (only on transitions). */
    int quad=use_proven && planar && !inventory && quad_rows_doubled(),quad_leave=0;
    if(quad_shown && !quad) {
        next_page=2; offset=2*DOS_VGA_PLANE_BYTES; skip_hud=0; quad_leave=1;
        planar_hud_valid[0]=planar_hud_valid[1]=0;
        chunky_hud_valid[0]=chunky_hud_valid[1]=0;
        ++dos_quad_transitions;
    } else if(quad && !quad_shown && shown_page!=2) {
        quad=0;
        if(next_page!=2) { quad_wait_flip(); latch_pending=0; }
        next_page=2; offset=2*DOS_VGA_PLANE_BYTES; skip_hud=0;
    } else if(quad && !quad_shown) {
        if(latch_pending) { quad_wait_flip(); latch_pending=0; }
        quad_hud_valid=0;
        ++dos_quad_transitions;
    }
#endif
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED)
    if(!inventory) inventory_shadow_valid=0;
#if defined(DOS_VGA_PACK_ALL)
    if(!inventory) inventory_canvas_valid=0;
#endif
    if(inventory && inventory_shadow_valid && inventory_shadow_page==shown_page) {
        if(!source || pitch<320) {
            dos_video_restore(); fputs("Invalid inventory framebuffer\n",stderr); exit(1);
        }
#ifdef DOSCRAFT_BENCH
        stamp=bench_us();
#endif
        et4000_upload_inventory(source,pitch,shown_page*DOS_VGA_PLANE_BYTES*4u);
        inventory_shadow=source;
        inventory_shadow_pitch=pitch;
#ifdef DOSCRAFT_BENCH
        dos_bench_upload+=bench_us()-stamp;
#endif
        chunky_hud_valid[shown_page]=0;
        planar_hud_valid[shown_page]=0;
        return;
    }
#endif
#ifdef DOS_VGA_ASYNC_FLIP
    /* Before reusing a page, ensure a retrace has passed since the flip that
     * replaced it (the second most recent); see flip_may_be_pending. */
    if(flip_count>=2 && flip_may_be_pending()) {
#ifdef DOSCRAFT_BENCH
        stamp=bench_us();
#endif
        video_wait_vsync();
#ifdef DOSCRAFT_BENCH
        dos_bench_wait+=bench_us()-stamp;
        if(!video_in_vsync()) ++dos_bench_timeouts;
#endif
        latch_pending=0;
    }
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
    if(use_pack_plan) {
        proven_count=
#ifdef DOS_LOW_PLANAR
            planar ? dos_vga_proven_pack_plan_planar(source,pitch,packed,sizeof(packed),
                         use_proven ? low_row_proven : no_row_proof,planar,
                         upload_runs,DOS_VGA_UPLOAD_MAX_RUNS,
#ifdef DOS_VGA_HUD_PROOF
                         skip_hud
#else
                         0
#endif
                         ) :
#endif
#ifdef DOS_VGA_HUD_PROOF
            skip_hud ? dos_vga_proven_pack_plan_view(source,pitch,packed,sizeof(packed),
                                               low_row_proven,upload_runs,DOS_VGA_UPLOAD_MAX_RUNS) :
#endif
            dos_vga_proven_pack_plan(source,pitch,packed,sizeof(packed),
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
        count=use_pack_plan ? proven_count :
            dos_vga_upload_plan(packed,DOS_VGA_PLANE_BYTES,200,upload_runs,DOS_VGA_UPLOAD_MAX_RUNS);
#else
        count=dos_vga_upload_plan(packed,DOS_VGA_PLANE_BYTES,200,upload_runs,DOS_VGA_UPLOAD_MAX_RUNS);
#endif
        if(count<0) { dos_video_restore(); fputs("Invalid VGA upload plan\n",stderr); exit(1); }
#ifdef DOS_VGA_SCAN_QUAD
        if(quad) {
            unsigned int qnext=(quad_page+1)%3u,qoff=QUAD_PAGE(qnext);
            for(r=0;r<count;++r) {
                const struct DosVgaRun *run=&upload_runs[r];
                unsigned int y=run->row+(run->row&1u),end=run->row+run->rows,first=y>>1,n=0;
                /* Gather the run's even rows into consecutive quad rows (plus
                 * the two repeats of row 99), then one transfer per run. */
                for(;y<end;y+=2) {
                    const unsigned char *src=packed+run->plane*DOS_VGA_PLANE_BYTES+y*80u;
                    memcpy(quad_gather+n*80u,src,80); ++n;
                    if(y==198) {
                        memcpy(quad_gather+n*80u,src,80); ++n;
                        memcpy(quad_gather+n*80u,src,80); ++n;
                    }
                }
                if(!n) continue;
                outportw(0x3c4,(run->mask<<8)|2);
                dosmemput(quad_gather,n*80u,0xa0000+qoff+first*80u);
            }
            if(!skip_hud) for(plane=0;plane<4;++plane) {
                const unsigned char *hud=packed+plane*DOS_VGA_PLANE_BYTES+200*80;
                if(dos_vga_hud_changed(hud,quad_hud_shadow[plane],DOS_VGA_HUD_CACHE_MAX_BYTES)
                   || !quad_hud_valid) {
                    if(!quad_hud_valid) memcpy(quad_hud_shadow[plane],hud,DOS_VGA_HUD_CACHE_MAX_BYTES);
                    outportw(0x3c4,((1u<<plane)<<8)|2);
                    dosmemput(hud,DOS_VGA_HUD_CACHE_MAX_BYTES,0xa0000);
                }
            }
            quad_hud_valid=1;
#ifdef DOSCRAFT_BENCH
            dos_bench_upload+=bench_us()-stamp;
#endif
            outportw(0x3c4, 0x0f02);
            quad_post_flip(qoff);
            prev_flip_at=last_flip_at;
            last_flip_at=wait_clock();
            if(fine_guard) {
                prev_flip_tick=flip_tick; prev_flip_count2=flip_count2;
                pit2_snapshot(&flip_tick,&flip_count2);
                if(flip_fine_valid<2) ++flip_fine_valid;
            }
            ++flip_count;
            latch_pending=1;
            quad_page=qnext; quad_shown=1; ++dos_quad_frames;
            return;
        }
#endif
        for(r=0;r<count;++r) {
            const struct DosVgaRun *run=&upload_runs[r];
            unsigned int row_offset=run->row*80u;
            outportw(0x3c4,(run->mask<<8)|2);
            dosmemput(packed+run->plane*DOS_VGA_PLANE_BYTES+row_offset,
                      run->rows*80u,0xa0000+offset+row_offset);
        }
        for(plane=0;plane<4;++plane) {
            const unsigned char *hud=packed+plane*DOS_VGA_PLANE_BYTES+200*80;
            if(
#ifdef DOS_VGA_HUD_PROOF
               !skip_hud &&
#endif
               (dos_vga_hud_changed(hud,hud_shadow[next_page][plane],DOS_VGA_HUD_CACHE_MAX_BYTES)
#ifdef DOS_ET4000_CHUNKY
               || !planar_hud_valid[next_page]
#endif
               )) {
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
    /* All three page starts are multiples of 256 bytes per plane, so the
     * low CRTC start register stays zero. One high-register write programs
     * the next page; the display latches it at its next frame boundary. */
#ifdef DOS_VGA_ASYNC_FLIP
#ifdef DOS_VGA_SCAN_QUAD
    quad_post_flip(offset);
#else
    outportw(0x3d4, (offset & 0xff00) | 0x0c);
#endif
    prev_flip_at=last_flip_at;
    last_flip_at=wait_clock();
    if(fine_guard) {
        prev_flip_tick=flip_tick; prev_flip_count2=flip_count2;
        pit2_snapshot(&flip_tick,&flip_count2);
        if(flip_fine_valid<2) ++flip_fine_valid;
    }
    ++flip_count;
    latch_pending=1;
#else
    /* Legacy synchronous reference: wait for the retrace that latches it. */
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
#endif
    shown_page = next_page;
#ifdef DOS_VGA_SCAN_QUAD
    if(quad_leave) {
        quad_wait_flip();     /* Page 2 shown from the next frame on. */
        latch_pending=0; quad_shown=0;
    }
#endif
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED)
    if(inventory) {
        inventory_shadow=source;
        inventory_shadow_pitch=pitch;
        inventory_shadow_page=shown_page;
        inventory_shadow_valid=1;
    }
#endif
}

int dos_video_last_present_proven(void)
{
#ifdef DOS_RASTER_FINISH_SKIP
    return last_proven_present;
#else
    return 0;
#endif
}

int dos_video_verify(const unsigned char *source, unsigned int pitch)
{
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED) && defined(DOS_VGA_PACK_ALL)
    inventory_canvas_valid=0; /* Readback uses the same packing scratch. */
#endif
    unsigned int plane;
    unsigned int offset = shown_page * DOS_VGA_PLANE_BYTES;
    unsigned int display_start;
#ifdef DOS_VGA_ASYNC_FLIP
    if(latch_pending) {
#ifdef DOS_VGA_SCAN_QUAD
        quad_wait_flip();
#endif
        video_wait_vsync();
        latch_pending=0;
    }
#endif
    outportb(0x3d4, 0x0c);
    display_start = inportb(0x3d5) << 8;
    outportb(0x3d4, 0x0d);
    display_start |= inportb(0x3d5);
#ifdef DOS_VGA_SCAN_QUAD
    if (quad_shown) {
        unsigned int y;
        if (display_start != QUAD_PAGE(quad_page)) return -1;
        for (plane = 0; plane < 4; ++plane) {
            if (dos_vga_pack_plane(source, pitch, packed, sizeof(packed), plane)) return -1;
            outportw(0x3ce, (plane << 8) | 4);
            dosmemget(0xa0000 + QUAD_PAGE(quad_page), QUAD_ROWS*80u, readback);
            for (y = 0; y < QUAD_ROWS; ++y)
                if (memcmp(readback + y*80u, packed + (y < 100 ? 2u*y : 198u)*80u, 80)) return -1;
            dosmemget(0xa0000, DOS_VGA_HUD_CACHE_MAX_BYTES, readback);
            if (memcmp(readback, packed + 200u*80u, DOS_VGA_HUD_CACHE_MAX_BYTES)) return -1;
        }
        outportw(0x3ce, 0x0004);
        return 0;
    }
#endif
    if (display_start != offset) return -1;
    for (plane = 0; plane < 4; ++plane) {
        if (dos_vga_pack_plane(source, pitch, packed, sizeof(packed), plane)) return -1;
#ifdef DOS_RASTER_FINISH_SKIP
        if (last_proven_present) {
            unsigned int y;
            for (y=1; y<200; y+=2)
                if ((low_row_proven[y]&3u)==3u)
                    memcpy(packed+y*80,packed+(y-1)*80,80);
        }
#endif
#ifdef DOS_VERY_LOW
        if (last_very_low_present) {
            unsigned int y,x;
            for (y=0; y<200; ++y) if (low_row_proven[y]&1u) {
                const unsigned char *src=source+(size_t)((y&1u) && (low_row_proven[y]&2u) ? y-1 : y)*pitch;
                for (x=0; x<80; ++x) packed[y*80+x]=src[x*4];
            }
        }
#endif
        outportw(0x3ce, (plane << 8) | 4);
        dosmemget(0xa0000 + offset, sizeof(readback), readback);
        if (memcmp(packed, readback, DOS_VGA_PLANE_BYTES)) return -1;
    }
    outportw(0x3ce, 0x0004);
    return 0;
}

#ifdef DOS_VERY_LOW
void dos_video_complete_very_low_source(unsigned char *source, unsigned int pitch)
{
    unsigned int y,x;
    if (!last_very_low_present || !source || pitch<320) return;
    for (y=0; y<200; ++y) if (low_row_proven[y]&1u) {
        unsigned char *dst=source+(size_t)y*pitch;
        const unsigned char *src=source+(size_t)((y&1u) && (low_row_proven[y]&2u) ? y-1 : y)*pitch;
        for (x=0; x<80; ++x) {
            unsigned char c=src[4*x];
            dst[4*x]=dst[4*x+1]=dst[4*x+2]=dst[4*x+3]=c;
        }
    }
}
#endif
