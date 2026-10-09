/* Standalone display gate; not a playable game or a game UI change. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <time.h>
#include "vga_pack.h"
#include "video_backend.h"

static unsigned char framebuffer[512 * DOS_VGA_HEIGHT];
int main(int argc, char **argv)
{
    unsigned int x, y;
    int matched, automatic = argc > 1 && !strcmp(argv[1], "/AUTO");
    FILE *report;
    uclock_t start;
    memset(framebuffer, 0xcd, sizeof(framebuffer));
    for (y = 0; y < DOS_VGA_HEIGHT; ++y)
        for (x = 0; x < DOS_VGA_WIDTH; ++x)
            framebuffer[y * 512 + x] = (unsigned char)((x / 4 + y) & 255);
    /* A border and a distinct last row reveal crop/pitch/mode errors. */
    for (x = 0; x < DOS_VGA_WIDTH; ++x) {
        framebuffer[x] = 255;
        framebuffer[(DOS_VGA_HEIGHT - 1) * 512 + x] = 127;
    }
    for (y = 0; y < DOS_VGA_HEIGHT; ++y) {
        framebuffer[y * 512] = 255;
        framebuffer[y * 512 + DOS_VGA_WIDTH - 1] = 255;
    }
    video_init();
    for (x = 0; x < 256; ++x) video_set_palette(x, x, 255 - x, x ^ 128);
    dos_video_present(framebuffer, 512);
    matched = dos_video_verify(framebuffer, 512) == 0;
    start = uclock();
    do {
        if (kbhit() && getch() == 27) break;
    } while (!automatic || uclock() - start < 3 * UCLOCKS_PER_SEC);
    dos_video_restore();
    report = fopen("C:\\DISPLAY.TXT", "wb");
    if (!report) report = fopen("A:\\DISPLAY.TXT", "wb");
    if (!report) return 1;
    fprintf(report, "DOScraft direct-port display gate\r\nWIDTH=320\r\nHEIGHT=240\r\nSOURCE_PITCH=512\r\nVRAM_PLANES=%s\r\nRESULT=%s\r\n",
            matched ? "PASS" : "FAIL", matched ? "PASS" : "FAIL");
    fclose(report);
    printf("320x240 DOS display: %s (requires visual border/aspect verification too)\n", matched ? "PASS" : "FAIL");
    return matched ? 0 : 1;
}
