/* Private temporary-file transport diagnostic; not world save/load or FPS.
 * No raw controller writes. All data changes stay in the prepared TERRAIN.TMP. */
#include "sys.h"
#include "system.h"
#include "hdd.h"
#include "hdd_backend.h"
#include <stdio.h>

#define SECTOR_COUNT (1u + 256u * 24u * 2u)
#define SLOT_A (1u + 255u * 24u * 2u)
#define SLOT_B (SLOT_A + 24u)
static u8 a[12288], b[12288], readback[12288], header[512];
static int run_request(u32 sector, int writing, u8 *bytes, int *calls)
{
    int status, guard = 64;
    *calls = 0;
    do {
        ++*calls;
        status = hdd_transfer(sector,24,writing,bytes);
    } while (!status && --guard);
    return status == 1;
}
int main(void)
{
    int marker = 0, capacity = 0, polled = 0, busy = 0, reopen = 0;
    int bounds = 0, isolation = 0, cancel = 0, cleanup = 0, ok;
    int i, writes_a = 0, writes_b = 0, reads_b = 0, unused;
    u32 elapsed, start;
    FILE *report;
    puts("DOScraft DOS temporary terrain file diagnostic (not the game)");
    sys_init();
    start = g_ticks;
    if (hdd_init()) {
        capacity = hdd_sector_count() == SECTOR_COUNT;
        marker = hdd_transfer_sync(0,1,0,header) == 1 &&
            !memcmp(header,"TSC-TERRAIN-TEMP",16) && ((u32 *)header)[4] == 1 &&
            ((u32 *)header)[5] == 256 && ((u32 *)header)[6] == 24;
        for (i=28;i<512;++i) if (header[i]) marker=0;
    }
    /* Unknown marker/size: do not write any terrain bytes. */
    if (capacity && marker) {
        bounds = hdd_transfer(SECTOR_COUNT,1,1,a) == -1 && hdd_error() == HDD_ERROR_CAPACITY;
        bounds &= hdd_transfer(SECTOR_COUNT-1,2,1,a) == -1 && hdd_error() == HDD_ERROR_CAPACITY;
        bounds &= hdd_transfer(0xFFFFFFFFu,24,0,a) == -1 && hdd_error() == HDD_ERROR_CAPACITY;
        bounds &= hdd_transfer(0,25,0,a) == -1 && hdd_error() == HDD_ERROR_ARGUMENT;
        for (i=0;i<12288;++i) {
            a[i]=(u8)(i*37+(i>>8)); b[i]=(u8)(i*11+(i>>7)+91);
        }
        polled = run_request(SLOT_A,1,a,&writes_a);
        polled &= run_request(SLOT_B,1,b,&writes_b);
        polled &= run_request(SLOT_B,0,readback,&reads_b) && !memcmp(b,readback,12288);
        polled &= writes_a >= 6 && writes_b >= 6 && reads_b >= 6;
        busy = hdd_transfer(SLOT_A,24,0,readback) == 0;
        busy &= hdd_transfer(SLOT_B,24,0,readback) == -1 && hdd_error() == HDD_ERROR_BUSY;
        busy &= hdd_transfer_sync(SLOT_A,24,0,readback) == 1 && !memcmp(a,readback,12288);
        /* Cancellation releases the buffer; previous slices may have reached
         * disk. The alternate completed slot must remain untouched. */
        memset(readback,0x5A,sizeof(readback));
        cancel = hdd_transfer(SLOT_A,24,1,readback) == 0;
        hdd_cancel();
        cancel &= hdd_error() == HDD_ERROR_CANCELLED;
        memset(readback,0,sizeof(readback));
        isolation = run_request(SLOT_B,0,readback,&unused) && !memcmp(b,readback,12288);
        cancel &= hdd_transfer_sync(SLOT_A,24,0,readback) == 1;
        for (i=0;i<12288;++i) if (readback[i] != (i<2048 ? 0x5A : a[i])) cancel=0;
        dos_hdd_shutdown();
        reopen = !hdd_sector_count() && hdd_error() == HDD_ERROR_NONE && hdd_init();
        reopen &= hdd_transfer_sync(SLOT_B,24,0,readback) == 1 && !memcmp(b,readback,12288);
        marker &= hdd_transfer_sync(0,1,0,readback) == 1 && !memcmp(header,readback,512);
    }
    dos_hdd_shutdown();
    cleanup = !hdd_sector_count();
    dos_hdd_shutdown();
    cleanup &= !hdd_sector_count();
    elapsed = g_ticks - start;
    dos_system_shutdown();
    ok = capacity && marker && polled && busy && reopen && bounds && isolation && cancel && cleanup;
    report = fopen("C:\\STORAGE.TXT","wb");
    if (!report) return 1;
    fprintf(report,"DOScraft DOS terrain transport\r\nSECTORS=%u\r\n"
        "CAPACITY=%s\r\nMARKER_UNCHANGED=%s\r\nPOLLED_24_SECTORS=%s\r\nBUSY_IDENTITY=%s\r\n"
        "FLUSH_REOPEN=%s\r\nBOUNDS=%s\r\nSLOT_ISOLATION=%s\r\nCANCEL_RELEASE=%s\r\n"
        "CLEANUP=%s\r\nWRITE_A_POLLS=%d\r\nWRITE_B_POLLS=%d\r\nREAD_B_POLLS=%d\r\n"
        "ELAPSED_100HZ_TICKS=%u\r\nWORLD_SAVE_LOAD=UNTESTED\r\nRESULT=%s\r\n",
        SECTOR_COUNT,capacity?"PASS":"FAIL",marker?"PASS":"FAIL",polled?"PASS":"FAIL",
        busy?"PASS":"FAIL",reopen?"PASS":"FAIL",bounds?"PASS":"FAIL",isolation?"PASS":"FAIL",
        cancel?"PASS":"FAIL",cleanup?"PASS":"FAIL",writes_a,writes_b,reads_b,elapsed,ok?"PASS":"FAIL");
    fclose(report);
    puts(ok ? "DOS terrain file: PASS" : "DOS terrain file: FAIL (see C:\\STORAGE.TXT)");
    return !ok;
}
