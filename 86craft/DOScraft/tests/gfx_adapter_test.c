#include "gfx.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static u8 uploaded[FB_PITCH * SCR_H];
static int upload_count;
void dos_video_present(const unsigned char *source, unsigned int pitch)
{
    int y;
    assert(pitch == FB_PITCH);
    for (y = 0; y < SCR_H; ++y)
        memcpy(uploaded + y * FB_PITCH, source + y * pitch, SCR_W);
    ++upload_count;
}

int main(void)
{
    u8 *page[3];
    int i, x, y;
    gfx_init();
    assert(gfx_back_page() == 1);
    for (i = 0; i < 256; ++i)
        assert(g_darkenLUT[i] == ((i & 0xf0) | ((i & 15) >> 1)));
    for (i = 0; i < 3; ++i) {
        int id = gfx_back_page();
        page[id] = g_fb;
        memset(g_fb, 0xcd, FB_PITCH * SCR_H);
        gfx_clear(g_fb, (u8)(10 + id));
        for (y = 0; y < SCR_H; ++y)
            for (x = SCR_W; x < FB_PITCH; ++x)
                assert(g_fb[y * FB_PITCH + x] == 0xcd);
        gfx_present();
        for (y = 0; y < SCR_H; ++y)
            for (x = 0; x < SCR_W; ++x)
                assert(uploaded[y * FB_PITCH + x] == 10 + id);
        assert(g_fb != page[id]);
    }
    assert(page[0] != page[1] && page[1] != page[2] && page[0] != page[2]);
    assert(gfx_back_page() == 1 && g_fb == page[1]);
    /* A page-specific cached HUD survives cycling back to that page. */
    assert(g_fb[VIEW_H * FB_PITCH] == 11);
    gfx_clear(g_fb, 0);
    gfx_rect(g_fb, -3, -4, 8, 9, 0x7f);
    assert(g_fb[0] == 0x7f && g_fb[4 * FB_PITCH + 4] == 0x7f);
    assert(g_fb[5] == 0 && g_fb[5 * FB_PITCH] == 0);
    gfx_frame(g_fb, 318, 238, 4, 4, 0x9f);
    assert(g_fb[238 * FB_PITCH + 319] == 0x9f);
    gfx_darken(g_fb, 0, 0, 1, 1);
    assert(g_fb[0] == 0x77);
    gfx_char(g_fb, 319, 239, 'A', 0x2f);
    gfx_text_shadow(g_fb, 0, VIEW_H, "DOS", 0x3f, 0x11);
    gfx_text_center(g_fb, VIEW_H + 16, "TOWNS", 0x4f, 0x10);
    gfx_wait_flip();
    gfx_present();
    gfx_sync_pages();
    for (i = 0; i < 3; ++i) {
        for (y = 0; y < SCR_H; ++y) {
            assert(!memcmp(page[i] + y * FB_PITCH, uploaded + y * FB_PITCH, SCR_W));
            for (x = SCR_W; x < FB_PITCH; ++x)
                assert(page[i][y * FB_PITCH + x] == 0xcd);
        }
    }
    assert(upload_count == 4);
    puts("PASS: three pages, retained HUD, clipping, text, padding, sync and transport");
    return 0;
}
