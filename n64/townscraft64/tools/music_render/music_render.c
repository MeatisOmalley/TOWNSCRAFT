/* Render DOScraft's OPL2 music to IMA ADPCM for the N64 (host tool).
 *
 * DOScraft's opl_music.c is included unchanged; its port writes go to
 * Nuked-OPL3 (LGPL-2.1, build/deps/nuked-opl3) in OPL2 mode instead of the
 * Sound Blaster's chip. Each piece (every MUSIC.PAK song, then the built-in
 * Towns piece) is started directly and ticked at 100 Hz exactly as the
 * timer IRQ does, until the sequencer finishes it (including its ring-out),
 * producing mono 16-bit samples at RATE. Output: MUSIC.N64, read by
 * src/n64/music.c:
 *   header  "T64MUS01", u32 rate, u32 pieces, u32 builtin piece index
 *   pieces  u32 offset, u32 samples, u32 bytes, char name[20]   (big-endian)
 *   data    IMA ADPCM, 4 bits/sample, high nibble first, predictor and
 *           step index starting at 0 for each piece.
 * Usage: music_render MUSIC.PAK OUT.N64 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "opl3.h"

#define RATE 21978              /* 2 x DOScraft's 10989 Hz effects mixer */

/* ---- What opl_music.c expects from DOS ---- */
static opl3_chip chip;
static int opl_index, timer_started;
volatile unsigned char g_keyDown[128];
volatile unsigned int g_ticks;
int g_musicOn = 1, g_sfxOn = 1;
static void host_outportb(unsigned short port, unsigned char v)
{
    if (port == 0x388) { opl_index = v; return; }
    if (port == 0x389) {
        if (opl_index == 4) timer_started = (v == 0x21);
        OPL3_WriteReg(&chip, opl_index, v);
    }
}
/* opl_detect: status 0 before the timer starts, then timer 1 expired. */
static unsigned char host_inportb(unsigned short port) { (void)port; return timer_started ? 0xC0 : 0x00; }
unsigned long dos_keyboard_irq_save(void) { return 0; }
void dos_keyboard_irq_restore(unsigned long s) { (void)s; }

/* The real music pack path is a DOS drive; read the one given instead. */
static const char *pack_file;
static FILE *host_fopen(const char *path, const char *mode)
{
    (void)path;
    return fopen(pack_file, mode);
}

#define outportb host_outportb
#define inportb host_inportb
#define fopen host_fopen
#include "opl_music.c"
#undef fopen

/* ---- IMA ADPCM ---- */
static const int step_table[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
    130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767 };
static const int index_table[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int predictor, index; } Adpcm;

static int adpcm_encode(Adpcm *s, int sample)
{
    int step = step_table[s->index], diff = sample - s->predictor, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    int delta = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1; delta += step; }
    s->predictor += (code & 8) ? -delta : delta;
    if (s->predictor > 32767) s->predictor = 32767;
    if (s->predictor < -32768) s->predictor = -32768;
    s->index += index_table[code & 7];
    if (s->index < 0) s->index = 0;
    if (s->index > 88) s->index = 88;
    return code;
}

static void put32be(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

typedef struct { uint32_t offset, samples, bytes; char name[20]; } Piece;

/* Start piece `index` (or BUILTIN) as start_piece() would after choosing it. */
static void begin(int index)
{
    current = index; last_piece = index;
    playing = 1; music_tick = 0; next_event = 0;
    if (current == BUILTIN) {
        for (int ch = 0; ch < 6; ++ch) set_patch(ch, towns_patch);
        return;
    }
    song = pack_songs + (unsigned)current * PACK_SONG;
    song_events = u32le(song + 24); song_next = 0; song_tail = SONG_TAIL;
    song_wait = u16le(pack_events + u32le(song + 20));
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: music_render MUSIC.PAK OUT.N64\n"); return 2; }
    pack_file = argv[1];
    OPL3_Reset(&chip, RATE);
    if (dos_sound_assets_build() < 0 || dos_music_init() != 0 || !pack_song_count) {
        fprintf(stderr, "music init failed (pack %s)\n", argv[1]);
        return 1;
    }
    int pieces = (int)pack_song_count + (pack_builtin ? 1 : 0);
    Piece *table = calloc(pieces, sizeof(Piece));
    size_t cap = 1u << 24, used = 0;
    unsigned char *data = malloc(cap);
    for (int p = 0; p < pieces; p++) {
        int index = p < (int)pack_song_count ? p : BUILTIN;
        snprintf(table[p].name, sizeof(table[p].name), "%s", dos_music_song_name(index));
        table[p].offset = (uint32_t)used;
        begin(index);
        Adpcm st = { 0, 0 };
        uint32_t samples = 0;
        double owed = 0;
        int nibble = -1;
        while (playing) {
            dos_music_tick();
            ++g_ticks;
            owed += RATE / 100.0;
            while (owed >= 1.0) {
                int16_t lr[2];
                OPL3_GenerateResampled(&chip, lr);
                int code = adpcm_encode(&st, ((int)lr[0] + lr[1]) / 2);
                if (nibble < 0) nibble = code << 4;
                else {
                    if (used == cap) data = realloc(data, cap *= 2);
                    data[used++] = (unsigned char)(nibble | code);
                    nibble = -1;
                }
                samples++;
                owed -= 1.0;
            }
        }
        if (nibble >= 0) data[used++] = (unsigned char)nibble;
        while (used % 8) data[used++] = 0;   /* keep ROM DMA addresses aligned */
        table[p].samples = samples;
        table[p].bytes = (uint32_t)used - table[p].offset;
        printf("%-20s %7.1f s  %8u bytes\n", table[p].name, samples / (double)RATE, table[p].bytes);
        /* Let release tails die between pieces, as the gap would. */
        for (int i = 0; i < RATE; i++) { int16_t lr[2]; OPL3_GenerateResampled(&chip, lr); }
    }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); return 1; }
    unsigned char head[20];
    memcpy(head, "T64MUS01", 8);
    put32be(head + 8, RATE);
    put32be(head + 12, (uint32_t)pieces);
    put32be(head + 16, pack_builtin ? (uint32_t)pack_song_count : 0xFFFFFFFFu);
    fwrite(head, 1, sizeof(head), out);
    uint32_t base = (uint32_t)(sizeof(head) + pieces * 32 + 4);   /* + pad: data starts 8-aligned */
    for (int p = 0; p < pieces; p++) {
        unsigned char e[32] = { 0 };
        put32be(e, base + table[p].offset);
        put32be(e + 4, table[p].samples);
        put32be(e + 8, table[p].bytes);
        memcpy(e + 12, table[p].name, 20);
        fwrite(e, 1, sizeof(e), out);
    }
    fwrite("\0\0\0\0", 1, 4, out);
    fwrite(data, 1, used, out);
    fclose(out);
    printf("%d pieces, %zu bytes of ADPCM\n", pieces, used);
    return 0;
}
