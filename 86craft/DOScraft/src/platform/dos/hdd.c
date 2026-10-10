/* DOS file transport for the unchanged hdd_* sector API. No SCSI/DMA access.
 * The caller still checks the scratch marker and owns slot/checksum publication.
 * One file call copies at most 2048 bytes per poll; DOS seeks/I/O/flush can
 * themselves block. This is NOT a hard latency bound or asynchronous disk I/O.
 * Missing files are never created, resized, formatted or silently replaced. */
#include "hdd.h"
#include "hdd_backend.h"
#include "file_commit.h"
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

typedef char dos_hdd_seek_width[(sizeof(long)==4 && sizeof(off_t)==4) ? 1 : -1];
static int terrain_fd = -1, last_error, cleanup_registered;
static u32 sectors;
static struct {
    int active, write;
    u32 lba, count, copied;
    u8 *buffer;
} request;

int hdd_error(void) { return last_error; }
u32 hdd_sector_count(void) { return sectors; }

void hdd_cancel(void)
{
    if (!request.active) return;
    request.active = 0;
    last_error = HDD_ERROR_CANCELLED;
    /* Previous write slices may already have reached disk. No retry/rollback. */
}

void dos_hdd_shutdown(void)
{
    hdd_cancel();
    if (terrain_fd >= 0 && close(terrain_fd)) last_error = HDD_ERROR_STATUS;
    terrain_fd = -1;
    sectors = 0;
}

int hdd_init(void)
{
    struct stat info;
    int fd;
    if (sectors) return 1;
    last_error = HDD_ERROR_NONE;
    fd = open(DOS_TERRAIN_PATH, O_RDWR | O_BINARY);
    if (fd < 0) { last_error = HDD_ERROR_ABSENT; return 0; }
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 512 ||
            info.st_size > LONG_MAX || info.st_size % HDD_SECTOR_BYTES) {
        close(fd);
        last_error = HDD_ERROR_CAPACITY;
        return 0;
    }
    if (!cleanup_registered) {
        if (atexit(dos_hdd_shutdown)) {
            close(fd);
            last_error = HDD_ERROR_MEMORY;
            return 0;
        }
        cleanup_registered = 1;
    }
    terrain_fd = fd;
    sectors = (u32)info.st_size / HDD_SECTOR_BYTES;
    return 1;
}

static int fail_transfer(int error)
{
    request.active = 0;
    last_error = error;
    return -1;
}

int hdd_transfer(u32 lba, u32 count, int write_request, u8 *buffer)
{
    u32 bytes, amount, offset;
    ssize_t copied;
    if (request.active) {
        if (lba != request.lba || count != request.count ||
                write_request != request.write || buffer != request.buffer) {
            last_error = HDD_ERROR_BUSY;
            return -1; /* Original request retains its buffer and progress. */
        }
    } else {
        if (!sectors) { last_error = HDD_ERROR_ABSENT; return -1; }
        if (!buffer || !count || count > HDD_MAX_SECTORS ||
                (write_request != 0 && write_request != 1)) {
            last_error = HDD_ERROR_ARGUMENT;
            return -1;
        }
        if (lba >= sectors || count > sectors - lba) {
            last_error = HDD_ERROR_CAPACITY;
            return -1;
        }
        request.active = 1;
        request.lba = lba;
        request.count = count;
        request.write = write_request;
        request.buffer = buffer;
        request.copied = 0;
        last_error = HDD_ERROR_NONE;
    }
    bytes = count * HDD_SECTOR_BYTES;
    offset = lba * HDD_SECTOR_BYTES + request.copied;
    /* Signed seek range was checked at open; subtraction bounds guard above
     * means neither multiply/add nor the final byte can overflow that range. */
    if (lseek(terrain_fd, (off_t)offset, SEEK_SET) != (off_t)offset)
        return fail_transfer(HDD_ERROR_STATUS);
    amount = bytes - request.copied;
    if (amount > 2048u) amount = 2048u;
    if (write_request) copied = write(terrain_fd, buffer + request.copied, amount);
    else copied = read(terrain_fd, buffer + request.copied, amount);
    /* Positive short transfers progress; EOF, zero progress or any DOS error
     * terminate. No unbounded EINTR/full-disk retry or stale success. */
    if (copied <= 0 || (u32)copied > amount) return fail_transfer(HDD_ERROR_STATUS);
    request.copied += (u32)copied;
    if (request.copied < bytes) return 0;
    /* Caller must not publish a new slot after a failed DOS commit. This is a
     * DOS ordering boundary, not proof of physical power-loss durability. */
    if (write_request && dos_file_commit(terrain_fd)) return fail_transfer(HDD_ERROR_STATUS);
    request.active = 0;
    last_error = HDD_ERROR_NONE;
    return 1;
}

int hdd_transfer_sync(u32 lba, u32 count, int write_request, u8 *buffer)
{
    int status;
    do { status = hdd_transfer(lba, count, write_request, buffer); } while (!status);
    return status;
}
