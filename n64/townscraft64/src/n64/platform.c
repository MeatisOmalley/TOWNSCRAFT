/* N64 platform layer for DOScraft's game: the Towns sys.h/video.h services
 * and DOScraft's DOS hooks, on libdragon.
 *
 * - Memory: DOScraft's heap (heap.c) over arenas sized as DOScraft sizes
 *   them for the same installed RAM (system.c): 4 MB console = 4 MiB tier,
 *   Expansion Pak = 8 MiB tier.
 * - Time: g_ticks at 100 Hz from a CPU timer, as the Towns interval timer.
 * - Display: the game draws 320x240 8-bit pages (DOScraft gfx.c); each
 *   present is one RDP blit through a 256-entry RGBA5551 TLUT, so the CPU
 *   does no pixel conversion.
 * - Input: see input.c. */
#include <libdragon.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
#include "gfx.h"
#include "heap.h"

volatile u32 g_ticks;
volatile u32 g_vsyncCount;
u32 g_ramMB;
int dos_cpu386;                 /* Graphics presets: not a 386 */
int g_profEnable;               /* DOScraft's sampling profiler toggle (F3); */
u32 g_profCount;                /* no N64 sampler yet */

void n64_input_init(void);
void n64_input_frame(void);
static void (*tick_hook)(void);

/* DOScraft audio registers its 100 Hz sequencer/mixer hook here (irq.h). */
void dos_irq_set_tick_hook(void (*hook)(void)) { tick_hook = hook; }

static uint16_t palette[256] __attribute__((aligned(16)));
static int palette_dirty = 1;

static void tick(int overflow)
{
    (void)overflow;
    ++g_ticks;
    if (tick_hook)
        tick_hook();
}

static void vblank(void)
{
    ++g_vsyncCount;
}

void fatal(const char *msg)
{
    assertf(0, "Townscraft64: %s", msg);
    for (;;) {}
}

void n64_platform_init(void)
{
    debug_init_isviewer();
    timer_init();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_DISABLED);
    rdpq_init();
    register_VI_handler(vblank);
    n64_input_init();

    /* DOScraft system.c arena tiers for the large world. */
    unsigned low, high;
    if (get_memory_size() >= 8 * 1024 * 1024) {
        g_ramMB = 8;
        low = 672u * 1024u;
        high = 3072u * 1024u;
    } else {
        g_ramMB = 4;
        low = 896u * 1024u;
        high = 768u * 1024u;
    }
    unsigned char *arena = malloc(low + high + 15);
    if (!arena)
        fatal("Cannot allocate game arenas");
    arena = (unsigned char *)(((uintptr_t)arena + 15) & ~(uintptr_t)15);
    if (dos_heap_init(arena, low, arena + low, high))
        fatal("Cannot bind game arenas");

    new_timer(TIMER_TICKS(10000), TF_CONTINUOUS, tick);   /* 100 Hz */
}

void sys_init(void)
{
    /* Everything is up before kmain (n64_platform_init). */
}

/* ---- video.h ---- */

void video_init(void) {}

void video_set_palette(int idx, int r, int g, int b)
{
    if (idx < 0 || idx > 255)
        return;
    palette[idx] = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
    palette_dirty = 1;
}

int video_in_vsync(void) { return 0; }

void video_wait_vsync(void)
{
    u32 n = g_vsyncCount;
    while (g_vsyncCount == n) {}
}

/* ---- DOScraft video backend hooks ---- */

void dos_video_present(const u8 *fb, unsigned int pitch)
{
    n64_input_frame();
    surface_t *disp = display_get();
    data_cache_hit_writeback(fb, pitch * SCR_H);
    if (palette_dirty) {
        data_cache_hit_writeback(palette, sizeof(palette));
        palette_dirty = 0;
    }
    surface_t src = surface_make((void *)fb, FMT_CI8, SCR_W, SCR_H, pitch);
    rdpq_attach(disp, NULL);
    rdpq_set_mode_copy(false);
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_tex_upload_tlut(palette, 0, 256);
    rdpq_tex_blit(&src, 0, 0, NULL);
    rdpq_detach_show();
}

void dos_video_set_inventory_hint(void) {}
/* No inventory canvas cache: the game redraws the inventory normally. */
int dos_video_inventory_canvas_save(const unsigned char *s, unsigned int p) { (void)s; (void)p; return 0; }
int dos_video_inventory_canvas_restore(unsigned char *t, unsigned int p) { (void)t; (void)p; return 0; }

/* ---- DOS-only game hooks ---- */

void dos_game_inventory_idle(void) {}   /* DOS smoke-test hook */

/* "Quit" from the main menu: there is no DOS to return to. */
void dos_game_exit(void)
{
    gfx_clear(g_fb, 0);
    gfx_text_center(g_fb, 116, "You can turn off the console now.", 15, 0);
    gfx_present();
    for (;;) {}
}
