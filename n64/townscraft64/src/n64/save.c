/* World saves on the N64: DOScraft's save codec over a RAM track transport,
 * persisted to the cartridge's 128 KiB FlashRAM as a delta against the ROM
 * world (OPEN_QUESTIONS Q6).
 *
 * The pinned Towns codec (towns_save_codec.inc, unchanged) writes a byte
 * stream in 8 KiB tracks, two alternating banks. Here tracks live in RAM.
 * After a successful save the newest bank is packed for FlashRAM:
 *   - the stream header and player/inventory state, verbatim;
 *   - only the column-directory entries that differ from the ROM's base
 *     index (the rest are rebuilt from the ROM on load);
 *   - chests and mobs, verbatim;
 *   - for each edited column, its blocks XORed against the ROM base column,
 *     coded as zero runs and literals. A few placed blocks cost a few bytes.
 * On first use after boot the FlashRAM image rebuilds the exact stream in
 * bank 0 and writes each edited column record back where the directory says
 * it lives (the V2 edit pool, in rom_hdd.c's RAM overlay), so DOScraft's own
 * load path runs unchanged. Edited columns are re-encoded with the column
 * codec's deterministic rules and must match the saved length and checksum. */
#include <libdragon.h>
#include <stdlib.h>
#include <string.h>
#include "save.h"
#include "sys.h"
#include "world.h"
#include "player.h"
#include "inventory.h"
#include "game.h"
#include "mobs.h"
#include "hdd.h"

int world_save_large_columns(void (*put)(u32));
int world_load_large_columns(u32 (*get)(void));
void world_commit_large_columns(void);

/* Transport constants, as DOScraft save.c. */
#define SECTOR_BYTES 1024
#define SECTORS_PER_TRACK 8
#define TRACK_BYTES (SECTOR_BYTES*SECTORS_PER_TRACK)
#define NUM_TRACKS (77*2)
#define SAVE_MAGIC 0x46435354
/* 6: 6-bit block IDs, items from 64. */
#define SAVE_VERSION 6

static int loadedMobs;
int save_loaded_mobs(void) { return loadedMobs; }
static u8 *trackBuf;
static int pageBufferedTrack = -1;
static int pageState, pageMotor;
static u8 *trackMem[NUM_TRACKS];

static int dos_save_finish(void) { return 1; }
static void fdc_stop(void) {}
static int dos_save_take_close_error(void) { return 0; }
static void page_cancel(void) { pageState = pageMotor = 0; pageBufferedTrack = -1; }
static int ensure_restored(void);

static int fdc_start(void)
{
    pageBufferedTrack = -1;
    if (!trackBuf && !(trackBuf = malloc(TRACK_BYTES)))
        return 0;
    return ensure_restored();
}

static int fdc_track(int track, int writing)
{
    pageBufferedTrack = -1;
    if (track < 0 || track >= NUM_TRACKS)
        return 0;
    if (writing) {
        if (!trackMem[track] && !(trackMem[track] = malloc(TRACK_BYTES)))
            return 0;
        memcpy(trackMem[track], trackBuf, TRACK_BYTES);
    } else if (trackMem[track]) {
        memcpy(trackBuf, trackMem[track], TRACK_BYTES);
    } else {
        memset(trackBuf, 0, TRACK_BYTES);
    }
    return 1;
}

void save_stream_tick(void) {}

/* Saved-column paging is the 256-world path; the large world never uses it. */
static int save_column_read(u32 offset, u32 length, u8 *dst)
{
    (void)offset; (void)length; (void)dst;
    return -1;
}

/* The codec's save_world is wrapped below to persist after it succeeds. */
#define save_world codec_save_world
#include "towns_save_codec.inc"
#undef save_world

/* ---- ROM base world ---- */

#define COLUMNS 4096u
#define CELLS COLUMN_CELLS
#define STATE_BYTES (16u * 4u + INV_SLOTS * 2u)
#define HEADER_BYTES 20u
#define DIR_OFFSET (HEADER_BYTES + STATE_BYTES)
#define DIR_BYTES (12u + COLUMNS * 16u)

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put_le32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

typedef struct { u32 off, len, sum; } BaseEntry;
static BaseEntry *base;         /* ROM index, loaded on demand */
static u32 poolSector;

static u32 fnv(const u8 *p, u32 n)
{
    u32 h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

static int read_bytes(u32 off, u32 len, u8 *dst)
{
    static u8 sector[512] __attribute__((aligned(16)));
    while (len) {
        u32 s = off / 512, at = off % 512, n = MIN(len, 512 - at);
        if (hdd_transfer_sync(s, 1, 0, sector) != 1)
            return 0;
        memcpy(dst, sector + at, n);
        dst += n; off += n; len -= n;
    }
    return 1;
}

static int load_base(void)
{
    if (base)
        return 1;
    u8 header[512];
    if (!hdd_init() || !read_bytes(0, 512, header) || memcmp(header, "DOSCRAFT-1024-V2", 16))
        return 0;
    u32 indexSector = le32(header + 31 * 4);
    poolSector = le32(header + 33 * 4);
    u8 *index = malloc(COLUMNS * 12);
    base = malloc(COLUMNS * sizeof(BaseEntry));
    if (!index || !base || !read_bytes(indexSector * 512, COLUMNS * 12, index)) {
        free(index); free(base); base = NULL;
        return 0;
    }
    for (u32 id = 0; id < COLUMNS; id++) {
        base[id].off = le32(index + id * 12);
        base[id].len = le32(index + id * 12 + 4);
        base[id].sum = le32(index + id * 12 + 8);
    }
    free(index);
    return 1;
}

/* Decode a column-codec record (raw, or count/block pairs) into CELLS bytes. */
static int decode(const u8 *rec, u32 len, u8 *cells)
{
    if (len == CELLS) { memcpy(cells, rec, CELLS); return 1; }
    u32 n = 0;
    for (u32 i = 0; i + 1 < len; i += 2) {
        if (!rec[i] || n + rec[i] > CELLS) return 0;
        memset(cells + n, rec[i + 1], rec[i]);
        n += rec[i];
    }
    return n == CELLS;
}

/* column_encode()'s rules: pairs, runs capped at 255, raw when no smaller. */
static u32 encode(const u8 *cells, u8 *rec)
{
    u32 runs = 0;
    int last = -1, n = 0;
    for (u32 i = 0; i < CELLS; i++) {
        if (cells[i] != last || n == 255) { runs++; last = cells[i]; n = 0; }
        n++;
    }
    if (runs * 2 >= CELLS) { memcpy(rec, cells, CELLS); return CELLS; }
    u8 *p = rec;
    last = -1; n = 0;
    for (u32 i = 0; i < CELLS; i++) {
        if (cells[i] != last || n == 255) {
            if (n) { *p++ = n; *p++ = last; }
            last = cells[i]; n = 0;
        }
        n++;
    }
    if (n) { *p++ = n; *p++ = last; }
    return p - rec;
}

static u32 record_offset(u32 id, u32 field)
{
    u32 slot = field & 3u, pair = field >> 2;
    return slot ? (poolSector + (pair * 2u + slot - 1u) * (CELLS / 512u)) * 512u : base[id].off;
}

/* Delta: XOR against the base, as [u16 zeros][u8 n][n literal bytes] tokens. */
static u32 delta_pack(const u8 *x, u8 *out)
{
    u8 *p = out;
    u32 i = 0;
    while (i < CELLS) {
        u32 z = 0;
        while (i < CELLS && !x[i] && z < 65535) { z++; i++; }
        u32 n = 0;
        while (i + n < CELLS && x[i + n] && n < 255) n++;
        *p++ = z; *p++ = z >> 8; *p++ = n;
        memcpy(p, x + i, n);
        p += n; i += n;
    }
    return p - out;
}

static int delta_apply(const u8 *in, u32 len, u8 *cells)
{
    u32 i = 0, at = 0;
    while (at < len) {
        if (at + 3 > len) return 0;
        u32 z = in[at] | in[at + 1] << 8, n = in[at + 2];
        at += 3; i += z;
        if (i + n > CELLS || at + n > len) return 0;
        for (u32 k = 0; k < n; k++) cells[i + k] ^= in[at + k];
        i += n; at += n;
    }
    return i == CELLS;
}

/* ---- FlashRAM image ---- */

#define IMAGE_MAGIC 0x54363453   /* 'T64S' */
#define IMAGE_VERSION 1
#define IMAGE_LIMIT (128u * 1024u)
typedef struct { u32 magic, version, bytes, sum; } ImageHeader;

static int flash_ready;
static int restored;

static int flash_init_once(void)
{
    if (!flash_ready)
        flash_ready = flashram_init(NULL, NULL) ? 1 : -1;
    return flash_ready > 0;
}

/* The newest bank: the valid header with the highest generation. */
static int newest_bank(void)
{
    int best = -1;
    u32 generation = 0;
    for (int bank = 0; bank < 2; bank++) {
        const u8 *t = trackMem[bank * BANK_TRACKS];
        if (t && le32(t) == SAVE_MAGIC && le32(t + 4) == SAVE_VERSION && le32(t + 16) >= generation) {
            generation = le32(t + 16);
            best = bank;
        }
    }
    return best;
}

static int stream_byte(int bank, u32 at, u8 *v)
{
    const u8 *t = trackMem[bank * BANK_TRACKS + at / TRACK_BYTES];
    if (!t) return 0;
    *v = t[at % TRACK_BYTES];
    return 1;
}

static int stream_copy(int bank, u32 at, u32 n, u8 *dst)
{
    for (u32 i = 0; i < n; i++)
        if (!stream_byte(bank, at + i, dst + i)) return 0;
    return 1;
}

static int persist(void)
{
    int bank = newest_bank();
    if (bank < 0 || !load_base() || !flash_init_once())
        return 0;
    u8 head[HEADER_BYTES];
    if (!stream_copy(bank, 0, HEADER_BYTES, head))
        return 0;
    u32 total = le32(head + 12) + HEADER_BYTES;
    if (total < DIR_OFFSET + DIR_BYTES)
        return 0;
    u8 *image = malloc(IMAGE_LIMIT), *cells = malloc(CELLS), *basecells = malloc(CELLS), *rec = malloc(CELLS);
    u8 *dir = malloc(DIR_BYTES);
    int ok = image && cells && basecells && rec && dir;
    u8 *p = image + sizeof(ImageHeader), *end = image + IMAGE_LIMIT;
#define NEED(n) do { if (p + (n) > end) { ok = 0; goto done; } } while (0)
    if (!ok) goto done;
    NEED(4 + DIR_OFFSET);
    put_le32(p, total); p += 4;
    if (!stream_copy(bank, 0, DIR_OFFSET, p)) { ok = 0; goto done; }
    p += DIR_OFFSET;
    if (!stream_copy(bank, DIR_OFFSET, DIR_BYTES, dir)) { ok = 0; goto done; }
    NEED(12 + 4);
    memcpy(p, dir, 12); p += 12;
    u8 *countAt = p; p += 4;
    u32 edited = 0;
    for (u32 id = 0; id < COLUMNS; id++) {
        const u8 *e = dir + 12 + id * 16;
        u32 field = le32(e), len = le32(e + 4), sum = le32(e + 8), raw = le32(e + 12);
        if (!field && len == base[id].len && sum == base[id].sum && raw == (len == CELLS))
            continue;
        NEED(2 + 16 + 4);
        *p++ = id; *p++ = id >> 8;
        memcpy(p, e, 16); p += 16;
        u32 deltaLen = 0;
        u8 *deltaAt = p; p += 4;
        if (field & 3u) {
            /* Current blocks from the edit record, XOR the ROM base. */
            if (!read_bytes(record_offset(id, field), len, rec) || !decode(rec, len, cells) ||
                !read_bytes(base[id].off, base[id].len, rec) || !decode(rec, base[id].len, basecells)) {
                ok = 0; goto done;
            }
            for (u32 i = 0; i < CELLS; i++) cells[i] ^= basecells[i];
            NEED(CELLS * 3 / 2);   /* worst case of the token coding */
            deltaLen = delta_pack(cells, p);
            p += deltaLen;
        }
        put_le32(deltaAt, deltaLen);
        edited++;
    }
    put_le32(countAt, edited);
    u32 tail = total - DIR_OFFSET - DIR_BYTES;
    NEED(4 + tail);
    put_le32(p, tail); p += 4;
    if (!stream_copy(bank, DIR_OFFSET + DIR_BYTES, tail, p)) { ok = 0; goto done; }
    p += tail;
    {
        u32 bytes = p - image - sizeof(ImageHeader);
        ImageHeader h = { IMAGE_MAGIC, IMAGE_VERSION, bytes, fnv(image + sizeof(ImageHeader), bytes) };
        memcpy(image, &h, sizeof(h));
        ok = flashram_write(image, 0, p - image) == (int)(p - image);
    }
done:
#undef NEED
    free(image); free(cells); free(basecells); free(rec); free(dir);
    return ok;
}

/* Rebuild bank 0 and the edited column records from FlashRAM, once per boot. */
static int restore(void)
{
    if (!flash_init_once())
        return 1;                  /* no save chip: empty slot */
    ImageHeader h;
    if (flashram_read(&h, 0, sizeof(h)) != (int)sizeof(h) || h.magic != IMAGE_MAGIC ||
        h.version != IMAGE_VERSION || h.bytes > IMAGE_LIMIT - sizeof(h))
        return 1;                  /* empty or foreign: empty slot */
    u8 *image = malloc(h.bytes), *stream = NULL, *cells = malloc(CELLS), *rec = malloc(CELLS);
    int ok = image && cells && rec && flashram_read(image, sizeof(h), h.bytes) == (int)h.bytes &&
             fnv(image, h.bytes) == h.sum && load_base();
    const u8 *p = image, *end = image + h.bytes;
    u32 total = 0;
    if (ok) {
        total = le32(p); p += 4;
        ok = total >= DIR_OFFSET + DIR_BYTES && total <= BANK_TRACKS * TRACK_BYTES &&
             (stream = calloc(1, total)) != NULL && p + DIR_OFFSET + 16 <= end;
    }
    if (ok) {
        memcpy(stream, p, DIR_OFFSET); p += DIR_OFFSET;
        u8 *dir = stream + DIR_OFFSET;
        memcpy(dir, p, 12); p += 12;
        for (u32 id = 0; id < COLUMNS; id++) {
            u8 *e = dir + 12 + id * 16;
            put_le32(e, 0); put_le32(e + 4, base[id].len); put_le32(e + 8, base[id].sum);
            put_le32(e + 12, base[id].len == CELLS);
        }
        u32 edited = le32(p); p += 4;
        for (u32 k = 0; ok && k < edited; k++) {
            if (p + 22 > end) { ok = 0; break; }
            u32 id = p[0] | p[1] << 8;
            const u8 *e = p + 2;
            u32 field = le32(e), len = le32(e + 4), sum = le32(e + 8), deltaLen = le32(e + 16);
            p += 22;
            if (id >= COLUMNS || p + deltaLen > end || len > CELLS) { ok = 0; break; }
            memcpy(dir + 12 + id * 16, e, 16);
            if (field & 3u) {
                /* Base blocks XOR delta, re-encoded where the directory points. */
                if (!read_bytes(base[id].off, base[id].len, rec) || !decode(rec, base[id].len, cells) ||
                    !delta_apply(p, deltaLen, cells) || encode(cells, rec) != len || fnv(rec, len) != sum) {
                    ok = 0; break;
                }
                u32 off = record_offset(id, field), sectors = (len + 511) / 512;
                static u8 pad[512] __attribute__((aligned(16)));
                for (u32 s = 0; ok && s < sectors; s++) {
                    u32 n = MIN(512, len - s * 512);
                    memset(pad, 0, 512);
                    memcpy(pad, rec + s * 512, n);
                    ok = hdd_transfer_sync(off / 512 + s, 1, 1, pad) == 1;
                }
            }
            p += deltaLen;
        }
        if (ok) {
            u32 tail = p + 4 <= end ? le32(p) : 0xFFFFFFFFu;
            p += 4;
            ok = tail == total - DIR_OFFSET - DIR_BYTES && p + tail <= end;
            if (ok) memcpy(stream + DIR_OFFSET + DIR_BYTES, p, tail);
        }
    }
    if (ok) {
        /* Publish as bank 0. */
        for (u32 t = 0; t * TRACK_BYTES < total; t++) {
            if (!trackMem[t] && !(trackMem[t] = malloc(TRACK_BYTES))) { ok = 0; break; }
            memset(trackMem[t], 0, TRACK_BYTES);
            memcpy(trackMem[t], stream + t * TRACK_BYTES, MIN(TRACK_BYTES, total - t * TRACK_BYTES));
        }
    }
    free(image); free(stream); free(cells); free(rec);
    return ok;
}

static int ensure_restored(void)
{
    if (!restored)
        restored = restore() ? 1 : -1;
    return restored > 0;
}

int save_world(void)
{
    int r = codec_save_world();
    if (r == SAVE_OK && !persist())
        r = SAVE_DISK_ERROR;
    return r;
}

/* Main-menu slot state; the header is little-endian on every platform. */
int dos_save_has_world(void)
{
    if (!fdc_start())
        return 0;
    return newest_bank() >= 0;
}

int dos_save_clear_world(void)
{
    if (!world_backing_idle())
        return SAVE_DISK_ERROR;
    page_cancel();
    for (int t = 0; t < NUM_TRACKS; t++) {
        free(trackMem[t]);
        trackMem[t] = NULL;
    }
    restored = 1;
    if (flash_init_once()) {
        ImageHeader h = { 0, 0, 0, 0 };
        if (flashram_write(&h, 0, sizeof(h)) != (int)sizeof(h))
            return SAVE_DISK_ERROR;
    }
    return SAVE_OK;
}
