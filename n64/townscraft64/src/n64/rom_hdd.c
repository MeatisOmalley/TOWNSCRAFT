/* hdd.h block device over the 1024-world medium in cartridge ROM.
 *
 * Reads DMA sectors straight from the medium's file in the ROM's DragonFS
 * image. The cartridge is read-only, so written sectors (dirty-column
 * eviction, edit records) live in a RAM overlay that later reads return.
 * The overlay is the simplest correct write path for running DOScraft's
 * world code unchanged; N5 replaces it with the block-level edit store and
 * FlashRAM saves (Q6).
 *
 * Same contract as the Towns/DOScraft drivers: one request at a time, 1..
 * HDD_MAX_SECTORS sectors, returns 1 complete or -1 error. Requests finish
 * immediately (PI DMA is synchronous here), so 0 "pending" is never
 * returned. */
#include <libdragon.h>
#include <stdlib.h>
#include <string.h>
#include "hdd.h"

#ifndef N64_TERRAIN_PATH
#define N64_TERRAIN_PATH "terrain.bin"
#endif

static pi_addr_t medium;            /* PI address of the medium in ROM */
static u32 sectors;
static u8 **overlay;                /* written sectors, by LBA; NULL = ROM */
static int last_error;

int hdd_error(void) { return last_error; }
u32 hdd_sector_count(void) { return sectors; }
void hdd_cancel(void) {}

int hdd_init(void)
{
    if (medium)
        return 1;
    static int dfs_ready;
    if (!dfs_ready) {
        if (dfs_init(DFS_DEFAULT_LOCATION) != DFS_ESUCCESS) {
            last_error = HDD_ERROR_ABSENT;
            return 0;
        }
        dfs_ready = 1;
    }
    int size = dfs_rom_size(N64_TERRAIN_PATH);
    pi_addr_t addr = dfs_rom_addr(N64_TERRAIN_PATH);
    if (!addr || size <= 0 || size % HDD_SECTOR_BYTES) {
        last_error = HDD_ERROR_ABSENT;
        return 0;
    }
    overlay = calloc(size / HDD_SECTOR_BYTES, sizeof(*overlay));
    if (!overlay) {
        last_error = HDD_ERROR_MEMORY;
        return 0;
    }
    medium = addr;
    sectors = size / HDD_SECTOR_BYTES;
    last_error = HDD_ERROR_NONE;
    return 1;
}

/* Reads sectors [lba, lba+count) that are not in the overlay with one DMA. */
static void read_rom(u32 lba, u32 count, u8 *buffer)
{
    u32 bytes = count * HDD_SECTOR_BYTES;
    data_cache_hit_writeback_invalidate(buffer, bytes);
    dma_read(buffer, medium + lba * HDD_SECTOR_BYTES, bytes);
}

int hdd_transfer(u32 lba, u32 count, int write, u8 *buffer)
{
    if (!medium) {
        last_error = HDD_ERROR_ABSENT;
        return -1;
    }
    if (count < 1 || count > HDD_MAX_SECTORS || lba >= sectors || count > sectors - lba) {
        last_error = HDD_ERROR_ARGUMENT;
        return -1;
    }
    if (write) {
        for (u32 i = 0; i < count; i++) {
            if (!overlay[lba + i] && !(overlay[lba + i] = malloc(HDD_SECTOR_BYTES))) {
                last_error = HDD_ERROR_MEMORY;
                return -1;
            }
            memcpy(overlay[lba + i], buffer + i * HDD_SECTOR_BYTES, HDD_SECTOR_BYTES);
        }
    } else {
        /* Runs of ROM sectors go in one DMA; overlay sectors are copied. */
        u32 i = 0;
        while (i < count) {
            if (overlay[lba + i]) {
                memcpy(buffer + i * HDD_SECTOR_BYTES, overlay[lba + i], HDD_SECTOR_BYTES);
                i++;
                continue;
            }
            u32 run = 1;
            while (i + run < count && !overlay[lba + i + run])
                run++;
            read_rom(lba + i, run, buffer + i * HDD_SECTOR_BYTES);
            i += run;
        }
    }
    last_error = HDD_ERROR_NONE;
    return 1;
}

int hdd_transfer_sync(u32 lba, u32 count, int write, u8 *buffer)
{
    return hdd_transfer(lba, count, write, buffer);
}

/* Frees the RAM overlay; the next hdd_init starts from the pristine ROM. */
void n64_hdd_shutdown(void)
{
    if (overlay) {
        for (u32 i = 0; i < sectors; i++)
            free(overlay[i]);
        free(overlay);
    }
    overlay = NULL;
    medium = 0;
    sectors = 0;
}
