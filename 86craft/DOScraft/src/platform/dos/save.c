/* Direct port: the original byte stream/codec is generated from pinned save.c.
 * Only its raw-track and cooperative paging transport is replaced here.
 * WORLD.SAV is an existing exact-size raw Towns save medium, NOT terrain scratch.
 * The single-slot menu clears published headers without resizing the medium. */
#include "save.h"
#include "sys.h"
#include "world.h"
#include "player.h"
#include "inventory.h"
#include "game.h"
#include "mobs.h"
#include "early_game.h"
#include "save_backend.h"
#include "file_commit.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef DOS_LARGE_WORLD
int world_save_large_columns(void (*put)(u32));
int world_load_large_columns(u32 (*get)(void),u32 version);
void world_commit_large_columns(void);
#endif

#define SECTOR_BYTES 1024
#define SECTORS_PER_TRACK 8
#define TRACK_BYTES (SECTOR_BYTES*SECTORS_PER_TRACK)
#define NUM_TRACKS (77*2)
#define SAVE_MAGIC 0x46435354
#ifdef DOS_LARGE_WORLD
/* 6: 6-bit block IDs, items from 64 (version 5 saves hold old item IDs).
   7: two development formats that never reached a player disk: per-block
      edits, or the early-game section; retired.
   8: per-block edits after the column directory and the early-game
      section after the mobs. Only version 8 loads. */
#define SAVE_VERSION 8
#else
#define SAVE_VERSION 4
#endif
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
 * One poll reads at most one 8-KiB track. Completion releases the destination:
 * each subsequent request recopies, even when its arguments match. The world
 * also uses that destination for terrain reads between saved-column reads. */
static int save_column_read(u32 offset, u32 length, u8 *dst)
{
    int same, track;
    u32 pos, amount;
    if (!dst || !length || length > COLUMN_CELLS ||
            offset > NUM_TRACKS*TRACK_BYTES ||
            length > NUM_TRACKS*TRACK_BYTES-offset) return -1;
    same = pageOffset == offset && pageLength == length && pageDest == dst;
    if (pageState == 1 && !same) return -1;
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

int dos_save_has_world(void)
{
    int found=0;
    page_cancel();
    if(!fdc_start()) return 0;
    for(int bank=0;bank<2;++bank) {
        if(fdc_track(bank*BANK_TRACKS,0)) {
            u32 magic,version;
            memcpy(&magic,trackBuf,4); memcpy(&version,trackBuf+4,4);
#ifdef DOS_LARGE_WORLD
            if(magic==SAVE_MAGIC && (version==SAVE_VERSION || version==6)) found=1;
#else
            if(magic==SAVE_MAGIC && version>=1 && version<=SAVE_VERSION &&
               (bank==0 || version>=4)) found=1;
#endif
        }
    }
    fdc_stop();
    return !dos_save_take_close_error() && found;
}

int dos_save_clear_world(void)
{
    int ok=1;
    if(!world_backing_idle()) return SAVE_DISK_ERROR;
    page_cancel();
    if(dos_save_take_close_error()) return SAVE_DISK_ERROR;
    if(!fdc_start()) return SAVE_NO_DISK;
    memset(trackBuf,0,TRACK_BYTES);
    /* Clear both banks: deleting only the latest would resurrect an older save. */
    for(int bank=0;bank<2;++bank)
        if(!fdc_track(bank*BANK_TRACKS,1)) { ok=0; break; }
    fdc_stop();
    if(dos_save_take_close_error()) ok=0;
    return ok ? SAVE_OK : SAVE_DISK_ERROR;
}
