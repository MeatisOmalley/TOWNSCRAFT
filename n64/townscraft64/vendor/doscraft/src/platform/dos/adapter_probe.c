/* Hardware verification of direct-port adapters, NOT a playable game. */
#include "gfx.h"
#include "heap.h"
#include "video_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned char low_storage[64] __attribute__((aligned(16)));
static unsigned char high_storage[1024] __attribute__((aligned(16)));
static unsigned char snapshot[FB_PITCH * SCR_H];

void fatal(const char *message)
{
    dos_video_restore();
    fprintf(stderr, "Adapter diagnostic failed: %s\n", message);
    exit(1);
}

int main(void)
{
    unsigned int mark;
    unsigned char *first, *drawn;
    int frame, i, y, transport = 1, retained = 1, memory;
    unsigned char expected[3] = {0, 0, 0}, seen[3] = {0, 0, 0};
    uclock_t start;
    FILE *report;
    if (dos_heap_init(low_storage, sizeof(low_storage), high_storage, sizeof(high_storage)))
        fatal("Arena binding");
    mark = heap_high_mark();
    first = heap_alloc_high(17);
    heap_high_rewind(mark);
    memory = heap_alloc_high(17) == first;
    dos_heap_shutdown();

    video_init();
    gfx_init();
    for (i = 0; i < 256; ++i)
        video_set_palette(i, (i & 15) * 17, ((i >> 4) & 15) * 17, (i & 15) * 17);
    for (frame = 0; frame < 9; ++frame) {
        int page = gfx_back_page();
        unsigned char color = (unsigned char)(0x20 + frame);
        if (seen[page] && g_fb[150 * FB_PITCH + 318] != expected[page]) retained = 0;
        gfx_clear(g_fb, color);
        gfx_frame(g_fb, 0, 0, SCR_W, SCR_H, 0x0f);
        gfx_text_center(g_fb, 16, "DOSCRAFT FRAMEBUFFER ADAPTER", 0x0f, 0);
        gfx_text_center(g_fb, 32, "DIAGNOSTIC - NOT THE GAME", 0x0f, 0);
        for (i = 0; i < 16; ++i)
            gfx_rect(g_fb, 32 + i * 16, 64, 16, 96, (unsigned char)(0x30 + i));
        gfx_rect(g_fb, 1, VIEW_H, SCR_W - 2, SCR_H - VIEW_H - 1, 0x10);
        gfx_text_center(g_fb, VIEW_H + 8, "ORIGINAL FONT / 40-ROW STRIP", 0x0f, 0);
        seen[page] = 1;
        expected[page] = color;
        drawn = g_fb;
        gfx_present();
        if (dos_video_verify(drawn, FB_PITCH)) transport = 0;
    }
    memcpy(snapshot, drawn, sizeof(snapshot));
    gfx_sync_pages();
    for (i = 0; i < 3; ++i) {
        for (y = 0; y < SCR_H; ++y)
            if (memcmp(g_fb + y * FB_PITCH, snapshot + y * FB_PITCH, SCR_W)) retained = 0;
        drawn = g_fb;
        gfx_present();
        if (dos_video_verify(drawn, FB_PITCH)) transport = 0;
    }
    start = uclock();
    while (uclock() - start < 3 * UCLOCKS_PER_SEC) {}
    dos_video_restore();
    report = fopen("C:\\ADAPTER.TXT", "wb");
    if (!report) report = fopen("A:\\ADAPTER.TXT", "wb");
    if (!report) return 1;
    fprintf(report, "DOScraft direct-port adapter diagnostic\r\nGFX_PRESENTATIONS=12\r\n"
            "GFX_TRANSFER=%s\r\nGFX_PAGE_STATE=%s\r\nHEAP_REWIND=%s\r\nRESULT=%s\r\n",
            transport ? "PASS" : "FAIL", retained ? "PASS" : "FAIL",
            memory ? "PASS" : "FAIL", transport && retained && memory ? "PASS" : "FAIL");
    fclose(report);
    printf("DOS framebuffer/heap adapters: %s (not the game)\n",
           transport && retained && memory ? "PASS" : "FAIL");
    return transport && retained && memory ? 0 : 1;
}
