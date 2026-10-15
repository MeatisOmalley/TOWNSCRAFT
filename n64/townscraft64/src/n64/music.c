/* DOScraft's music on the N64.
 *
 * DOScraft plays its pieces on an OPL2 chip (opl_music.c). The N64 has none,
 * so tools/render_music.py renders every piece offline through DOScraft's
 * unchanged sequencer and Nuked-OPL3, as IMA ADPCM at 21978 Hz
 * (rom:/MUSIC.N64). This file keeps DOScraft's scheduling exactly: a random
 * piece (never the previous one) after a random gap, music_schedule /
 * music_set_gap / music_stop, and F6 (g_musicOn) silencing at once.
 *
 * Playback: the main loop (n64_music_pump, once per presented frame) DMAs
 * ADPCM from ROM into a 16 KiB ring, about 1.5 s; the audio interrupt decodes
 * from it (n64_music_sample). A frame slower than the ring briefly drops the
 * music rather than stalling the game. */
#include <libdragon.h>
#include <string.h>
#include "sound.h"
#include "sys.h"
#include "opl_music.h"

#define RING 16384u
#define CHUNK 2048u
#define BUILTIN (-1)

typedef struct { uint32_t offset, samples, bytes; char name[21]; } Piece;

static Piece pieces[32];
static int piece_count, builtin_index = -1, ready;
static pi_addr_t rom;

/* Scheduler state, as opl_music.c. */
static volatile int playing, music_wait = -1;
static int gap = 6000, current = BUILTIN, last_piece = -2;
unsigned int dos_music_notes;

/* Stream state. Main loop owns `fill`/`fetched`, the interrupt owns `drain`
 * and the decoder; `primed` hands a new piece from interrupt to main loop. */
static uint8_t ring[RING] __attribute__((aligned(16)));
static volatile uint32_t fill, drain;           /* byte counts, wrap via % RING */
static volatile uint32_t fetched, decoded;      /* piece bytes fetched / samples decoded */
static volatile int stream_piece = -1, stream_generation, pumped_generation;
static int predictor, step_index, nibble_phase;
static uint8_t nibble_byte;

static const int16_t step_table[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
    130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767 };
static const int8_t index_table[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int dos_music_init(void)
{
    ready = 0;
    int size = dfs_rom_size("MUSIC.N64");
    rom = dfs_rom_addr("MUSIC.N64");
    if (!rom || size < 20)
        return -1;
    uint8_t head[20] __attribute__((aligned(16)));
    data_cache_hit_writeback_invalidate(head, sizeof(head));
    dma_read(head, rom, sizeof(head));
    piece_count = (int)be32(head + 12);
    if (memcmp(head, "T64MUS01", 8) || be32(head + 8) != 21978 || piece_count < 1 || piece_count > 32)
        return -1;
    builtin_index = be32(head + 16) == 0xFFFFFFFFu ? -1 : (int)be32(head + 16);
    static uint8_t table[32 * 32] __attribute__((aligned(16)));
    data_cache_hit_writeback_invalidate(table, sizeof(table));
    dma_read(table, rom + 20, piece_count * 32);
    for (int i = 0; i < piece_count; i++) {
        pieces[i].offset = be32(table + i * 32);
        pieces[i].samples = be32(table + i * 32 + 4);
        pieces[i].bytes = be32(table + i * 32 + 8);
        memcpy(pieces[i].name, table + i * 32 + 12, 20);
        pieces[i].name[20] = 0;
    }
    playing = 0;
    ready = 1;
    return 0;
}

const char *dos_music_backend(void) { return ready ? "N64 ADPCM" : "NONE"; }
unsigned int dos_music_songs(void) { return ready ? (unsigned)(piece_count - (builtin_index >= 0)) : 0; }
int dos_music_current(void) { return current; }
int dos_music_playing(void) { return ready && playing; }
const char *dos_music_song_name(int piece)
{
    if (piece < 0 || piece >= piece_count || piece == builtin_index) return "Towns piece";
    return pieces[piece].name;
}

void music_schedule(int ticks)
{
    disable_interrupts();
    if (!playing) music_wait = ticks;
    enable_interrupts();
}

void music_set_gap(int minimum) { gap = minimum; }

static void stop_stream(void)
{
    stream_piece = -1;
}

void music_stop(void)
{
    disable_interrupts();
    stop_stream();
    playing = 0;
    music_wait = -1;
    enable_interrupts();
}

/* opl_music.c choose_piece(): pack songs plus the built-in Towns piece. */
static int choose_piece(void)
{
    int songs = piece_count - (builtin_index >= 0);
    int builtin = builtin_index >= 0;
    unsigned choices = (unsigned)songs + (builtin ? 1u : 0u);
    unsigned pick = ((unsigned)g_ticks * 2654435761u >> 16) % choices;
    int piece = builtin && pick == (unsigned)songs ? BUILTIN : (int)pick;
    if (choices > 1 && piece == last_piece) {
        pick = (pick + 1) % choices;
        piece = builtin && pick == (unsigned)songs ? BUILTIN : (int)pick;
    }
    return piece;
}

/* Called from the timer interrupt (start) or with it masked (stop). */
static void start_piece(void)
{
    current = choose_piece();
    last_piece = current;
    playing = 1;
    int index = current == BUILTIN ? builtin_index : current;
    fill = drain = 0;
    fetched = decoded = 0;
    predictor = step_index = nibble_phase = 0;
    stream_piece = index;
    stream_generation++;
}

static void finish_piece(void)
{
    stop_stream();
    playing = 0;
    music_wait = gap + (int)(((unsigned)g_ticks * 2654435761u >> 16) % (unsigned)(gap + 1));
}

/* 100 Hz, from the timer interrupt (DOScraft's sound_tick). */
void dos_music_tick(void)
{
    if (!ready) return;
    if (!playing) {
        if (music_wait > 0) --music_wait;
        else if (!music_wait && g_musicOn) { music_wait = -1; start_piece(); }
        return;
    }
    if (!g_musicOn) { stop_stream(); playing = 0; music_wait = gap; return; }
    int index = stream_piece;
    if (index < 0 || decoded >= pieces[index].samples) finish_piece();
}

void dos_music_shutdown(void)
{
    ready = 0;
    music_stop();
}

/* Main loop, once per frame: top up the ring from ROM. */
void n64_music_pump(void)
{
    int index = stream_piece, generation = stream_generation;
    if (index < 0) return;
    if (generation != pumped_generation) {
        pumped_generation = generation;
    }
    const Piece *p = &pieces[index];
    while (fetched < p->bytes && fill - drain <= RING - CHUNK) {
        uint32_t at = fill % RING, n = CHUNK;
        if (n > p->bytes - fetched) n = p->bytes - fetched;
        data_cache_hit_writeback_invalidate(ring + at, CHUNK);
        dma_read(ring + at, rom + p->offset + fetched, (n + 1) & ~1u);
        if (stream_generation != generation) return;    /* restarted meanwhile */
        fetched += n;
        fill += n;
    }
}

/* Audio interrupt: next music sample, or 0 when silent or starved. */
int n64_music_sample(void)
{
    int index = stream_piece;
    if (index < 0 || decoded >= pieces[index].samples) return 0;
    if (!nibble_phase) {
        if (drain == fill) return 0;                    /* starved: skip */
        nibble_byte = ring[drain % RING];
        drain++;
    }
    int code = nibble_phase ? nibble_byte & 15 : nibble_byte >> 4;
    nibble_phase ^= 1;
    int step = step_table[step_index], delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    predictor += (code & 8) ? -delta : delta;
    if (predictor > 32767) predictor = 32767;
    if (predictor < -32768) predictor = -32768;
    step_index += index_table[code & 7];
    if (step_index < 0) step_index = 0;
    if (step_index > 88) step_index = 88;
    decoded++;
    return predictor;
}
