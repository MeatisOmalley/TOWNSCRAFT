/* Game-facing framebuffer transport. The renderer and drawing routines stay
 * unchanged. Three RAM pages preserve page-specific HUD/partial redraw state;
 * presentation synchronously uploads one page to the tested VGA backend. */
#include "gfx.h"
#include "video_backend.h"

static u8 pages[3][FB_PITCH * SCR_H];
static int back_page, presented_page;
u8 *g_fb;
u8 g_darkenLUT[256];
extern const u8 g_font8x8[95][8];

void gfx_init(void)
{
    int i;
    back_page = 1;
    presented_page = 0;
    g_fb = pages[back_page];
    for (i = 0; i < 256; ++i)
        g_darkenLUT[i] = (i & 0xf0) | ((i & 15) >> 1);
}

void gfx_present(void)
{
    dos_video_present(g_fb, FB_PITCH);
    presented_page = back_page;
    back_page = (presented_page + 1) % 3;
    g_fb = pages[back_page];
}

int gfx_back_page(void)
{
    return back_page;
}

void gfx_wait_flip(void)
{
    /* Upload is synchronous; the source RAM page is not visible VGA memory. */
}

void gfx_sync_pages(void)
{
    int page, y;
    for (page = 0; page < 3; ++page) {
        if (page == presented_page) continue;
        for (y = 0; y < SCR_H; ++y)
            memcpy(pages[page] + y * FB_PITCH,
                   pages[presented_page] + y * FB_PITCH, SCR_W);
    }
    back_page = (presented_page + 1) % 3;
    g_fb = pages[back_page];
}

/* Generated only from the checksum-verified pinned gfx.c: gfx_clear and
 * gfx_rect/frame/darken/char/text/shadow/center, with their bodies unchanged. */
#include "towns_gfx_draw.inc"
