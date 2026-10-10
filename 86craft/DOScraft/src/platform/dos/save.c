/* Direct port: the original byte stream/codec is generated from pinned save.c.
 * Only its raw-track and cooperative paging transport is replaced here.
 * WORLD.SAV is an existing exact-size raw Towns save medium, NOT terrain scratch.
 * No creation/truncation, format conversion, new save policy or UI changes. */
#include "save.h"
#include "sys.h"
#include "world.h"
#include "player.h"
#include "inventory.h"
#include "game.h"
#include "mobs.h"
#include "save_backend.h"
#include "file_commit.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTOR_BYTES 1024
#define SECTORS_PER_TRACK 8
#define TRACK_BYTES (SECTOR_BYTES*SECTORS_PER_TRACK)
#define NUM_TRACKS (77*2)
#define SAVE_MAGIC 0x46435354
#define SAVE_VERSION 4
static int loadedMobs;
int save_loaded_mobs(void) { return loadedMobs; }
static u8 *trackBuf;
static int pageBufferedTrack = -1;
static int save_fd = -1, cleanup_registered;
static int close_error;
static int pageState, pageMotor;
static u32 pageOffset, pageLength, pageDone, pageIdle;
static u8 *pageDest;

static void fdc_buffer(void)
{
    /* Keep the original reservation; DOS transport needs no DMA alignment. */
    if (!trackBuf) trackBuf = heap_alloc_low(TRACK_BYTES*2);
}

static int dos_save_finish(void)
{
    int ok = 1;
    if (save_fd >= 0 && close(save_fd)) ok = 0;
    save_fd = -1;
    return ok;
}

static void fdc_stop(void) { if (!dos_save_finish()) close_error = 1; }

static int dos_save_take_close_error(void)
{
    int error = close_error;
    close_error = 0;
    return error;
}

static void page_cancel(void)
{
    pageState = pageMotor = 0;
    pageDest = NULL;
    pageBufferedTrack = -1;
    fdc_stop();
}

void dos_save_shutdown(void) { page_cancel(); }

static int fdc_open(void)
{
    struct stat info;
    int fd;
    if (save_fd >= 0) return 1;
    fd = open(DOS_SAVE_PATH, O_RDWR | O_BINARY);
    /* A write-protected medium can still be loaded. Writes fail normally. */
    if (fd < 0 && (errno == EACCES || errno == EROFS))
        fd = open(DOS_SAVE_PATH, O_RDONLY | O_BINARY);
    if (fd < 0) return 0;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) ||
            info.st_size != NUM_TRACKS*TRACK_BYTES) {
        close(fd);
        return 0;
    }
    if (!cleanup_registered) {
        if (atexit(dos_save_shutdown)) { close(fd); return 0; }
        cleanup_registered = 1;
    }
    save_fd = fd;
    return 1;
}

static int fdc_start(void)
{
    pageBufferedTrack = -1;
    fdc_buffer();
    return fdc_open();
}

/* Exact positioned track I/O. Every successful write is committed before
 * returning to the unchanged header/checksum/bank-publication code. DOS I/O
 * may block; this is not an asynchronous or hard-latency-bounded driver. */
static int fdc_track(int track, int writing)
{
    unsigned int done = 0;
    off_t offset;
    pageBufferedTrack = -1;
    if (save_fd < 0 || track < 0 || track >= NUM_TRACKS) return 0;
    offset = (off_t)track * TRACK_BYTES;
    if (lseek(save_fd, offset, SEEK_SET) != offset) return 0;
    while (done < TRACK_BYTES) {
        unsigned int amount = TRACK_BYTES-done;
        ssize_t n = writing ? write(save_fd, trackBuf+done, amount) :
                              read(save_fd, trackBuf+done, amount);
        if (n <= 0 || (unsigned int)n > amount) return 0;
        done += (unsigned int)n;
    }
    return !writing || !dos_file_commit(save_fd);
}

void save_stream_tick(void)
{
    if (pageMotor && (pageState == 0 || pageState == 5 || pageState < 0) &&
            g_ticks-pageIdle > 200) {
        fdc_stop();
        pageMotor = 0;
    }
}

/* Preserve the original one-request destination ownership and track reuse.
 * One poll reads at most one 8-KiB track. Identical completed reads may reuse
 * their result; callers must cancel/reset before changing the owned buffer. */
static int save_column_read(u32 offset, u32 length, u8 *dst)
{
    int same, track;
    u32 pos, amount;
    if (!dst || !length || length > COLUMN_CELLS ||
            offset > NUM_TRACKS*TRACK_BYTES ||
            length > NUM_TRACKS*TRACK_BYTES-offset) return -1;
    same = pageOffset == offset && pageLength == length && pageDest == dst;
    if (pageState == 1 && !same) return -1;
    if (same && pageState == 5) { pageIdle = g_ticks; return 1; }
    if (same && pageState < 0 && g_ticks-pageIdle < 100) return -1;
    if (pageState != 1) {
        fdc_buffer();
        pageOffset = offset; pageLength = length; pageDest = dst;
        pageDone = 0; pageState = 1;
    }
    track = (pageOffset+pageDone)/TRACK_BYTES;
    if (track != pageBufferedTrack) {
        if (!fdc_open() || !fdc_track(track, 0)) {
            pageState = -1; pageIdle = g_ticks;
            return -1;
        }
        pageMotor = 1;
        pageBufferedTrack = track;
    }
    pos = (pageOffset+pageDone)%TRACK_BYTES;
    amount = MIN(pageLength-pageDone, TRACK_BYTES-pos);
    memcpy(pageDest+pageDone, trackBuf+pos, amount);
    pageDone += amount;
    if (pageDone == pageLength) { pageState = 5; pageIdle = g_ticks; return 1; }
    return 0;
}

/* Pinned byte-stream, checksum, RLE, v1-v4 state and source-bank protection.
 * Explicit codec edits propagate final and idle/cancel close failures. */
#include "towns_save_codec.inc"
