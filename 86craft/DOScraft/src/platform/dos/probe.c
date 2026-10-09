/* First PC platform gate, not the game or a new rendering option.
 * The timed VGA path is BIOS mode 13h + dosmemput. Gameplay/HUD parity will
 * use a separate backend; do not infer the final game's resolution from this.
 */
#include <dpmi.h>
#include <pc.h>
#include <sys/movedata.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

static int old_mode = -1;
static void mode(int number) {
    __dpmi_regs r;
    memset(&r, 0, sizeof(r));
    r.x.ax = number;
    __dpmi_int(0x10, &r);
}
static void restore(void) {
    if (old_mode >= 0) { mode(old_mode); old_mode = -1; }
}
static int disk_test(void) {
    unsigned char wanted[4096], got[4096];
    FILE *f;
    int i, block;
    /* The launcher creates a private scratch disk. Refuse unknown data. */
    f = fopen("C:\\PROBE.DAT", "rb");
    if (f) { fclose(f); return -2; }
    for (i = 0; i < 4096; ++i) wanted[i] = (unsigned char)(i * 17 + (i >> 8));
    f = fopen("C:\\PROBE.DAT", "wb");
    if (!f) return -1;
    for (block = 0; block < 8; ++block) {
        if (fwrite(wanted, 1, 4096, f) != 4096) { fclose(f); return -1; }
    }
    if (fclose(f)) return -1;
    f = fopen("C:\\PROBE.DAT", "rb");
    if (!f) return -1;
    for (block = 0; block < 8; ++block) {
        if (fread(got, 1, 4096, f) != 4096 || memcmp(got, wanted, 4096)) {
            fclose(f); return -1;
        }
    }
    if (fgetc(f) != EOF) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
int main(int argc, char **argv) {
    __dpmi_version_ret version;
    __dpmi_free_mem_info memory;
    __dpmi_regs r;
    unsigned char *heap, *pixels;
    FILE *log;
    unsigned short fpu_word = 0;
    int i, x, y, memory_ok = 1, disk, automatic = argc > 1 && !strcmp(argv[1], "/AUTO");
    uclock_t start, upload_ticks, elapsed;
    memset(&version, 0, sizeof(version));
    memset(&memory, 0, sizeof(memory));
    __dpmi_get_version(&version);
    __dpmi_get_free_memory_information(&memory);
    heap = malloc(8UL * 1024 * 1024);
    pixels = malloc(64000);
    if (!heap || !pixels) { puts("FAIL: platform allocation"); free(heap); free(pixels); return 1; }
    for (i = 0; i < 8 * 1024 * 1024; i += 4096) heap[i] = (unsigned char)(i / 4096);
    for (i = 0; i < 8 * 1024 * 1024; i += 4096)
        if (heap[i] != (unsigned char)(i / 4096)) memory_ok = 0;
    /* Profile requires a 387. This explicit instruction is not a renderer change. */
    __asm__ volatile ("fninit; fnstcw %0" : "=m" (fpu_word));
    disk = disk_test();
    memset(&r, 0, sizeof(r)); r.h.ah = 0x0f; __dpmi_int(0x10, &r);
    old_mode = r.h.al; atexit(restore); mode(0x13);
    outportb(0x3c8, 0);
    for (i = 0; i < 256; ++i) {
        outportb(0x3c9, i & 63); outportb(0x3c9, (i >> 2) & 63); outportb(0x3c9, 63 - (i & 63));
    }
    for (y = 0; y < 200; ++y) for (x = 0; x < 320; ++x)
        pixels[y * 320 + x] = (unsigned char)((x / 8) + (y / 8) * 4);
    start = uclock();
    for (i = 0; i < 16; ++i) dosmemput(pixels, 64000, 0xa0000);
    upload_ticks = uclock() - start;
    start = uclock();
    /* Interactive runs hold the pattern until ESC. Automated runs are bounded. */
    do {
        if (kbhit() && getch() == 27) break;
        elapsed = uclock() - start;
    } while (!automatic || elapsed < 2 * UCLOCKS_PER_SEC);
    restore();
    free(pixels); free(heap);
    log = fopen("C:\\PLATFORM.TXT", "wb");
    if (!log) log = fopen("A:\\PLATFORM.TXT", "wb");
    if (!log) { puts("FAIL: report file"); return 1; }
    fprintf(log, "DOScraft platform probe v1\r\nCPU_DPMI=%u\r\nFPU_CONTROL=%04x\r\n", version.cpu, fpu_word);
    fprintf(log, "FREE_BLOCK_BYTES=%lu\r\nPHYSICAL_PAGES=%lu\r\nSWAP_PAGES=%lu\r\n", memory.largest_available_free_block_in_bytes, memory.total_number_of_physical_pages, memory.size_of_paging_file_partition_in_pages);
    fprintf(log, "HEAP_8M=%s\r\nDISK_32K=%s\r\nVGA_UPLOADS=16\r\nVGA_TICKS=%lld\r\nTIMER_HZ=%ld\r\n", memory_ok ? "PASS" : "FAIL", disk == 0 ? "PASS" : "FAIL", upload_ticks, (long)UCLOCKS_PER_SEC);
    fprintf(log, "RESULT=%s\r\n", memory_ok && disk == 0 && version.cpu == 3 && fpu_word == 0x37f && upload_ticks > 0 ? "PASS" : "FAIL");
    fclose(log);
    printf("DOScraft platform probe: memory %s, C: disk %s, VGA tested.\n", memory_ok ? "PASS" : "FAIL", disk == 0 ? "PASS" : "FAIL");
    puts("Report: C:\\PLATFORM.TXT (or A: if C: unavailable).");
    return memory_ok && disk == 0 ? 0 : 1;
}
