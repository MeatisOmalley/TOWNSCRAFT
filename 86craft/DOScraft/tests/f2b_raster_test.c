/* Real pinned raster.c + trap.S are the image oracle. No simulated raster or
 * texel loop determines expected colors. This runs as native x86, not a VM.
 * Original exports are renamed only in their object; original trap.S keeps
 * its normal symbols and these eight __real_* stubs forward to that assembly.
 */
#include "raster.h"
#include "f2b_capture.h"
#include "f2b_raster.h"
#include "f2b_texel.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void reference_raster_set_target(u8 *, int, int, int, int);
void reference_raster_poly(const RVert *, int, const u8 *, int);
void reference_raster_flat_poly(const RVert *, int, u8);
void reference_raster_pixel(int, int, u8);
void reference_raster_finish(void);
extern int reference_g_fastFlat;
extern int reference_g_recip14[321];
extern int g_recip14[321];
extern u32 reference_g_statPixels;
void dos_f2b_opaque_span(unsigned char *, int, struct DosF2BTexel *, int);

#define FORWARD(name) \
    extern void name(struct DosF2BTrap *); \
    void __real_##name(struct DosF2BTrap *t) { name(t); }
FORWARD(trap_opaque1)
FORWARD(trap_opaque2)
FORWARD(trap_transp1)
FORWARD(trap_transp2)
FORWARD(trap_opaque1w)
FORWARD(trap_opaque2w)
FORWARD(trap_transp1w)
FORWARD(trap_transp2w)
#undef FORWARD
typedef void (*TrapFunction)(struct DosF2BTrap *);
static const TrapFunction originals[8] = {
    trap_opaque1, trap_opaque2, trap_transp1, trap_transp2,
    trap_opaque1w, trap_opaque2w, trap_transp1w, trap_transp2w
};
static const TrapFunction wrappers[8] = {
    __wrap_trap_opaque1, __wrap_trap_opaque2,
    __wrap_trap_transp1, __wrap_trap_transp2,
    __wrap_trap_opaque1w, __wrap_trap_opaque2w,
    __wrap_trap_transp1w, __wrap_trap_transp2w
};

#define GUARD 32
#define BYTES (512 * 240 + 2 * GUARD)
#define CANARY 197
static unsigned char expected[BYTES], actual[BYTES], probe_a[BYTES], probe_b[BYTES];
static unsigned char union_mask[320 * 200];
static struct DosF2BCapture capture;
static unsigned char *allocation, *page, *tile, *zero_tile;
static unsigned rng = 0x19973821u;
static int width, height, scale, pitch, case_id;
static const char *mode;
static unsigned checks;

static void fail(const char *condition, int line)
{
    fprintf(stderr, "FAIL %s case=%d scale=%d width=%d height=%d pitch=%d line=%d: %s\n",
            mode, case_id, scale, width, height, pitch, line, condition);
    exit(1);
}
#define CHECK(condition) do { ++checks; if (!(condition)) fail(#condition, __LINE__); } while (0)
static unsigned next(void)
{ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void same_image(void)
{
    int i;
    for (i = 0; i < BYTES; ++i) if (expected[i] != actual[i]) {
        fprintf(stderr, "image byte %d: painter=%u candidate=%u\n", i,
                (unsigned)expected[i], (unsigned)actual[i]);
        fail("whole framebuffer identity", __LINE__);
    }
}

/* Audit padding, both external guards, and all rows outside the viewport,
 * independently of image equality (both renderers could otherwise overrun).
 * Before finish, scale-2 odd rows must also remain untouched.
 */
static void guards(const unsigned char *buffer, int finished)
{
    int i, row, rows = height * scale;
    for (i = 0; i < GUARD; ++i) CHECK(buffer[i] == CANARY);
    for (row = 0; row < rows; ++row) {
        int start = (scale == 2 && !finished && (row & 1)) ? 0 : width * scale;
        for (i = start; i < pitch; ++i) CHECK(buffer[GUARD + row * pitch + i] == CANARY);
    }
    for (i = GUARD + rows * pitch; i < BYTES; ++i) CHECK(buffer[i] == CANARY);
}

static void targets(int w, int h, int p, int s)
{
    width = w; height = h; pitch = p; scale = s;
    memset(expected, CANARY, sizeof(expected));
    memset(actual, CANARY, sizeof(actual));
    reference_raster_set_target(expected + GUARD, w, h, p, s);
    raster_set_target(actual + GUARD, w, h, p, s);
    CHECK(memcmp(reference_g_recip14, g_recip14, sizeof(reference_g_recip14)) == 0);
}

static void original_draw(const struct DosF2BCommand *cmd, void *context)
{
    (void)context;
    if (cmd->kind == DOS_F2B_TEXTURED)
        reference_raster_poly(cmd->vertices, cmd->count, cmd->tile, cmd->flags);
    else if (cmd->kind == DOS_F2B_FLAT)
        reference_raster_flat_poly(cmd->vertices, cmd->count, (u8)cmd->color);
    else reference_raster_pixel(cmd->vertices[0].x, cmd->vertices[0].y, (u8)cmd->color);
}

static void candidate_draw(const struct DosF2BCommand *cmd, void *context)
{
    (void)context;
    if (cmd->kind == DOS_F2B_TEXTURED)
        raster_poly(cmd->vertices, cmd->count, cmd->tile, cmd->flags);
    else if (cmd->kind == DOS_F2B_FLAT)
        raster_flat_poly(cmd->vertices, cmd->count, (u8)cmd->color);
    else raster_pixel(cmd->vertices[0].x, cmd->vertices[0].y, (u8)cmd->color);
}

/* The original painter on two backgrounds identifies actual opaque writes,
 * including opaque color zero, without assuming coverage from geometry/UVs.
 */
static unsigned primitive_union(void)
{
    int i, x, y;
    unsigned count = 0;
    memset(union_mask, 0, sizeof(union_mask));
    for (i = 0; i < capture.count; ++i) {
        memset(probe_a, 0, sizeof(probe_a));
        memset(probe_b, 255, sizeof(probe_b));
        reference_raster_set_target(probe_a + GUARD, width, height, pitch, scale);
        original_draw(&capture.commands[i], NULL);
        reference_raster_set_target(probe_b + GUARD, width, height, pitch, scale);
        original_draw(&capture.commands[i], NULL);
        for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) {
            int at = GUARD + y * pitch * scale + x * scale;
            if (probe_a[at] == probe_b[at]) union_mask[y * width + x] = 1;
        }
    }
    for (i = 0; i < width * height; ++i) count += union_mask[i];
    return count;
}

static void rect(RVert v[4], int x, int y, int w, int h, int uv)
{
    v[0] = (RVert){x * 16, y * 16, 511, 32767};
    v[1] = (RVert){(x + w) * 16, y * 16, uv, 32767};
    v[2] = (RVert){(x + w) * 16, (y + h) * 16, uv, uv};
    v[3] = (RVert){x * 16, (y + h) * 16, 511, uv};
}

static void scenes(void)
{
    int scene, i, j, s;
    for (s = 1; s <= 2; ++s) for (scene = 0; scene < 64; ++scene) {
        unsigned covered;
        RVert v[4];
        case_id = scene;
        /* Include logical widths not divisible by 32 and non-power-two stride. */
        targets(scene % 4 ? 320 / s : 157, scene % 4 ? 200 / s : 93,
                scene & 1 ? 509 : 512, s);
        for (i = 0; i < height; ++i) for (j = 0; j < width; ++j) {
            int at = GUARD + i * pitch * scale + j * scale;
            unsigned char color = (unsigned char)((i * 11 + j * 7) % 256);
            expected[at] = actual[at] = color;
            if (s == 2) expected[at + 1] = actual[at + 1] = color;
        }
        reference_g_fastFlat = g_fastFlat = scene & 1;
        dos_f2b_capture_reset(&capture);
        for (i = 0; i < 64; ++i) {
            int flags = i % 4;
            int x = (int)(next() % (width + 80)) - 40;
            int y = (int)(next() % (height + 80)) - 40;
            int w = 1 + (int)(next() % width), h = 1 + (int)(next() % height);
            int skew = (int)(next() % 49) - 24;
            int uv = (flags & RP_WRAP) ? 65536 * (16 + (int)(next() % 225)) - 1 : 16 * 65536 - 1;
            rect(v, x, y, w, h, uv);
            v[2].x += skew * 16; v[3].x += skew * 16;
            for (j = 0; j < 4; ++j) { v[j].x += next() % 8; v[j].y += next() % 8; }
            if (i & 1) { RVert swap = v[1]; v[1] = v[3]; v[3] = swap; }
            if (i % 9 == 0) CHECK(dos_f2b_capture_point(&capture, x, y, 0) == 1);
            else if (i % 7 == 0) CHECK(dos_f2b_capture_flat(&capture, v, i % 11 ? 4 : 3, i % 14 ? 47 : 0) == 1);
            else CHECK(dos_f2b_capture_poly(&capture, v, i % 11 ? 4 : 3, tile, flags) == 1);
        }
        /* Foreground points deliberately split uncovered rows into singles,
         * odd/even runs, and gaps across coverage-word boundaries. */
        for (i = 0; i < 16; ++i)
            CHECK(dos_f2b_capture_point(&capture, (i * 31 + 1) % width,
                                       (i * 7 + 1) % height, i & 1 ? 0 : 79) == 1);
        if (scene % 8 == 0) {
            rect(v, -2, -2, width + 4, height + 4, 16 * 65536 - 1);
            CHECK(dos_f2b_capture_flat(&capture, v, 4, 0) == 1);
        } else if (scene % 8 == 1) {
            rect(v, 0, 0, width, height, 16 * 65536 - 1);
            CHECK(dos_f2b_capture_poly(&capture, v, 4, zero_tile, 0) == 1);
        } else if (scene % 8 == 2) {
            rect(v, 0, 0, width, height, 16 * 65536 - 1);
            CHECK(dos_f2b_capture_poly(&capture, v, 4, zero_tile, RP_TRANSPARENT) == 1);
        }
        covered = primitive_union();
        reference_raster_set_target(expected + GUARD, width, height, pitch, scale);
        reference_g_statPixels = g_statPixels = 0;
        CHECK(dos_f2b_capture_replay(&capture, 0, original_draw, NULL) == 1);
        CHECK(dos_f2b_begin(width, height) == 1);
        CHECK(dos_f2b_capture_replay(&capture, 1, candidate_draw, NULL) == 1);
        CHECK(reference_g_statPixels == g_statPixels);
        CHECK(dos_f2b_stats.covered == covered);
        CHECK(dos_f2b_full() == (covered == (unsigned)(width * height)));
        for (i = 0; i < 256; ++i) {
            int x = next() % width, y = next() % height;
            CHECK(dos_f2b_hidden_rect(x, y, x + 1, y + 1) == union_mask[y * width + x]);
        }
        if (scene % 8 < 2) {
            CHECK(dos_f2b_full());
            CHECK(dos_f2b_stats.skipped > 0);
        }
        guards(expected, 0); guards(actual, 0);
        dos_f2b_end();
        reference_raster_finish(); raster_finish();
        same_image(); guards(expected, 1); guards(actual, 1);
    }
    puts("PASS: 128 original-painter scenes, reverse bounded replay, dual-background coverage, whole images/guards, both scales");
}

static struct DosF2BTrap trap(unsigned char *row, int count, int rows, int variant)
{
    struct DosF2BTrap t;
    memset(&t, 0, sizeof(t));
    t.row = row; t.stride = pitch * scale; t.rows = rows;
    t.lx = (variant % 3 == 0 ? -9 : 1) * 65536 + (int)(next() % 65536);
    t.rx = t.lx + count * 65536 + (int)(next() % 32768);
    t.ldx = (int)(next() % 8193) - 4096; t.rdx = -t.ldx;
    t.lu = (int)(next() % (16 * 65536)); t.lv = (int)(next() % (16 * 65536));
    t.ru = (int)(next() % (128 * 65536)) - 64 * 65536;
    t.rv = (int)(next() % (128 * 65536)) - 64 * 65536;
    t.ldu = (int)(next() % 8193) - 4096; t.ldv = -t.ldu;
    t.rdu = (int)(next() % 8193) - 4096; t.rdv = -t.rdu;
    t.tile = page + (variant % 3 == 0 ? 0xfff0 : variant % 3 == 1 ? 0x2010 : 0);
    t.maxX = width; t.pixels = 0xfffffff0u;
    return t;
}

static void same_trap(struct DosF2BTrap a, struct DosF2BTrap b,
                      unsigned char *base_a, unsigned char *base_b)
{
    CHECK((uintptr_t)a.row - (uintptr_t)base_a == (uintptr_t)b.row - (uintptr_t)base_b);
    a.row = b.row;
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

static void traps(void)
{
    int variant, iteration, x, y;
    for (variant = 0; variant < 8; ++variant) for (iteration = 0; iteration < 512; ++iteration) {
        struct DosF2BTrap a, b, again;
        unsigned opaque = 0, claimed = 0, hidden_before;
        case_id = variant * 512 + iteration;
        targets(iteration & 1 ? 157 : 320 / (1 + (variant & 1)), 7,
                iteration & 2 ? 509 : 512, 1 + (variant & 1));
        a = trap(expected + GUARD, 1 + (int)(next() % 360),
                 iteration % 13 == 0 ? -(iteration % 3) : 4, iteration);
        b = a; b.row = actual + GUARD;
        dos_f2b_end();
        /* Inactive wrapper must forward every pixel and every Trap field,
         * even zero/negative row counts and overflowing pixel counters. */
        originals[variant](&a); wrappers[variant](&b);
        same_trap(a, b, expected, actual); same_image();
        guards(expected, 0); guards(actual, 0);

        targets(width, height, pitch, scale);
        a = trap(expected + GUARD, 1 + (int)(next() % 360), 4, iteration);
        b = a; b.row = actual + GUARD;
        memset(probe_a, 0, sizeof(probe_a)); memset(probe_b, 255, sizeof(probe_b));
        { struct DosF2BTrap p = a; p.row = probe_a + GUARD; originals[variant](&p); }
        { struct DosF2BTrap p = a; p.row = probe_b + GUARD; originals[variant](&p); }
        originals[variant](&a);
        CHECK(dos_f2b_begin(width, height) == 1);
        for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) {
            int at = GUARD + y * pitch * scale + x * scale;
            int covered = iteration % 11 == 0 || (x + y * 3) % 17 < 3 || x == 31 || x == 32 || x == 63;
            int written = probe_a[at] == probe_b[at];
            if (covered) {
                CHECK(dos_f2b_claim_point(x, y) == 1); ++claimed;
                expected[at] = actual[at] = 53;
                if (scale == 2) expected[at + 1] = actual[at + 1] = 53;
            } else if (written) ++opaque;
        }
        hidden_before = dos_f2b_stats.covered;
        again = b;
        wrappers[variant](&b);
        CHECK(dos_f2b_stats.covered == claimed + opaque);
        CHECK(hidden_before == claimed);
        CHECK(dos_f2b_stats.rows == 4);
        CHECK(dos_f2b_stats.sampled + dos_f2b_stats.skipped == a.pixels - again.pixels);
        if (iteration % 11 == 0) {
            CHECK(dos_f2b_stats.sampled == 0);
            CHECK(dos_f2b_stats.hidden_rows <= 4);
        }
        same_trap(a, b, expected, actual); same_image();
        guards(expected, 0); guards(actual, 0);
        /* All written/nonzero cutout texels are now hidden; zero cutouts
         * remain uncovered. A second draw must neither overwrite nor claim. */
        b = again;
        wrappers[variant](&b);
        CHECK(dos_f2b_stats.covered == claimed + opaque);
        CHECK(dos_f2b_stats.rows == 8);
        CHECK(dos_f2b_stats.sampled + dos_f2b_stats.skipped == 2 * (a.pixels - again.pixels));
        same_trap(a, b, expected, actual); same_image();
        b = again; b.rows = -2;
        again = b;
        wrappers[variant](&b);
        CHECK(memcmp(&b, &again, sizeof(b)) == 0);
        CHECK(dos_f2b_stats.rows == 8);
        dos_f2b_end();
    }
    puts("PASS: 4096 actual-trap cases, eight active/inactive wrappers, mutation/pixel-counter parity, sparse gaps and canaries");
}

static int32_t gradient(int32_t left, int32_t right)
{
    int32_t delta = (int32_t)((uint32_t)right - (uint32_t)left);
    return (int32_t)((uint32_t)(delta >> 8) * (uint32_t)g_recip14[320]) >> 6;
}

static void phase(void)
{
    static const int counts[] = {1, 2, 3, 4, 5, 31, 32, 33, 63, 64, 65, 159, 160, 319, 320};
    int variant, sample, size, x;
    for (variant = 0; variant < 4; ++variant) for (sample = 0; sample < 6; ++sample)
        for (size = 0; size < (int)(sizeof(counts) / sizeof(counts[0])); ++size) {
            struct DosF2BTrap t;
            struct DosF2BTexel initial, state;
            unsigned char whole[640 + 64], split[sizeof(whole)], gaps[sizeof(whole)];
            int count = counts[size], s = 1 + (variant & 1), wrap = variant >> 1;
            case_id = (variant * 6 + sample) * 15 + size;
            targets(320, 1, 768, s);
            memset(&t, 0, sizeof(t));
            t.row = expected + GUARD; t.stride = pitch * scale; t.rows = 1;
            t.lx = 0; t.rx = 320 * 65536; t.maxX = 320;
            t.tile = page + (sample % 3 == 0 ? 0xfff0 : sample % 3 == 1 ? 0x2010 : 0);
            t.lu = sample & 1 ? -1234567 : 0x0fffff;
            t.lv = sample & 1 ? 0x0bffff : -234567;
            t.ru = sample & 2 ? t.lu - 233 * 65536 : t.lu + 211 * 65536;
            t.rv = sample & 4 ? t.lv - 199 * 65536 : t.lv + 227 * 65536;
            CHECK(dos_f2b_texel_init(&initial, t.tile, t.lu, t.lv,
                                    gradient(t.lu, t.ru), gradient(t.lv, t.rv), wrap) == 1);
            originals[(wrap ? 4 : 0) + s - 1](&t);
            memset(whole, CANARY, sizeof(whole)); memset(split, CANARY, sizeof(split));
            memset(gaps, CANARY, sizeof(gaps));
            state = initial;
            dos_f2b_opaque_span(whole + GUARD, count, &state, s);
            CHECK(memcmp(whole + GUARD, expected + GUARD, count * s) == 0);
            /* A following real assembly store checks final phase after the
             * last texel, including singleton and odd-entry pair loops. */
            if (count < 320) {
                dos_f2b_opaque_span(whole + GUARD + count * s, 1, &state, s);
                CHECK(memcmp(whole + GUARD, expected + GUARD, (count + 1) * s) == 0);
            }
            state = initial;
            for (x = 0; x < count;) {
                int n = 1 + x % 7;
                if (n > count - x) n = count - x;
                dos_f2b_opaque_span(split + GUARD + x * s, n, &state, s); x += n;
            }
            CHECK(memcmp(split + GUARD, expected + GUARD, count * s) == 0);
            state = initial;
            for (x = 0; x < count;) {
                int n = 1 + x % 5, gap;
                if (n > count - x) n = count - x;
                dos_f2b_opaque_span(gaps + GUARD + x * s, n, &state, s);
                CHECK(memcmp(gaps + GUARD + x * s, expected + GUARD + x * s, n * s) == 0);
                x += n; gap = 1 + x % 37;
                if (gap > count - x) gap = count - x;
                CHECK(dos_f2b_texel_jump(&state, gap) == 1);
                while (gap--) {
                    CHECK(gaps[GUARD + x * s] == CANARY);
                    if (s == 2) CHECK(gaps[GUARD + x * s + 1] == CANARY);
                    ++x;
                }
            }
            for (x = 0; x < GUARD; ++x)
                CHECK(whole[x] == CANARY && split[x] == CANARY && gaps[x] == CANARY);
            for (x = GUARD + (count + (count < 320)) * s; x < (int)sizeof(whole); ++x)
                CHECK(whole[x] == CANARY);
            for (x = GUARD + count * s; x < (int)sizeof(split); ++x)
                CHECK(split[x] == CANARY && gaps[x] == CANARY);
        }
    puts("PASS: 360 actual-assembly phase cases, singles/odd/even, negative gradients, address-byte carries, split spans and gaps");
}

static void invalid_traps(void)
{
    int variant, invalid;
    for (variant = 0; variant < 8; ++variant) for (invalid = 0; invalid < 7; ++invalid) {
        struct DosF2BTrap a, b;
        targets(37, 3, invalid & 1 ? 509 : 512, 1 + (variant & 1));
        CHECK(dos_f2b_begin(width, height) == 1);
        a = trap(expected + GUARD, 20, 2, invalid);
        b = a; b.row = actual + GUARD;
        /* Draw a valid original trap to obtain its exact field advancement;
         * candidate rejects the corresponding wrong/off-target geometry. */
        originals[variant](&a);
        if (invalid == 0) b.row = actual + GUARD - 1;
        if (invalid == 1) b.row = actual + GUARD + 1;
        if (invalid == 2) b.row = actual + GUARD + height * pitch * scale;
        if (invalid == 3) ++b.stride;
        if (invalid == 4) dos_f2b_set_target(actual + GUARD, width, height, pitch, 3 - scale);
        if (invalid == 5) dos_f2b_set_target(actual + GUARD, width - 1, height, pitch, scale);
        if (invalid == 6) dos_f2b_set_target(NULL, width, height, pitch, scale);
        { struct DosF2BTrap before = b;
          wrappers[variant](&b);
          CHECK((uintptr_t)b.row == (uintptr_t)before.row + (unsigned)before.stride * before.rows);
          CHECK(b.pixels == a.pixels);
          CHECK(b.rows == 0 && b.lx == a.lx && b.rx == a.rx);
          CHECK(b.lu == a.lu && b.lv == a.lv && b.ru == a.ru && b.rv == a.rv);
          CHECK(b.stride == before.stride && b.maxX == before.maxX && b.tile == before.tile);
          CHECK(b.ldx == before.ldx && b.rdx == before.rdx && b.ldu == before.ldu &&
                b.ldv == before.ldv && b.rdu == before.rdu && b.rdv == before.rdv);
        }
        CHECK(dos_f2b_stats.covered == 0 && dos_f2b_stats.sampled == 0);
        { int i; for (i = 0; i < BYTES; ++i) CHECK(actual[i] == CANARY); }
        dos_f2b_end();
    }
}

static void bounds(void)
{
    static const int bad[][5] = {
        {0, 3, 512, 1, 0}, {321, 3, 512, 1, 0}, {37, 0, 512, 1, 0},
        {37, 201, 512, 1, 0}, {-1, 3, 512, 1, 0}, {37, -1, 512, 1, 0},
        {37, 3, 512, 0, 0}, {37, 3, 512, 3, 0}, {37, 3, 36, 1, 0},
        {37, 3, 73, 2, 0}, {37, 3, -1, 1, 0}, {37, 3, INT_MAX, 2, 0},
        {37, 3, 512, 1, 1}
    };
    int i, s, x, y;
    dos_f2b_end();
    CHECK(dos_f2b_begin(37, 3) == 0); /* No recorded target at startup. */
    CHECK(dos_f2b_claim_point(0, 0) == -1);
    CHECK(!dos_f2b_full() && !dos_f2b_hidden_rect(0, 0, 1, 1));
    for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); ++i) {
        case_id = i;
        targets(37, 3, 512, 1);
        CHECK(dos_f2b_begin(width, height) == 1);
        CHECK(dos_f2b_claim_point(0, 0) == 1);
        dos_f2b_set_target(bad[i][4] ? NULL : actual + GUARD,
                           bad[i][0], bad[i][1], bad[i][2], bad[i][3]);
        CHECK(dos_f2b_begin(bad[i][0], bad[i][1]) == 0);
        CHECK(!dos_f2b_active && !dos_f2b_full());
        CHECK(dos_f2b_stats.covered == 0);
        CHECK(dos_f2b_claim_point(0, 0) == -1);
        same_image();
    }
    for (s = 1; s <= 2; ++s) {
        RVert v[4];
        targets(37, 3, 509, s);
        CHECK(dos_f2b_begin(36, 3) == 0);
        CHECK(dos_f2b_begin(37, 2) == 0);
        CHECK(dos_f2b_begin(37, 3) == 1);
        CHECK(dos_f2b_claim_point(-1, 0) == -1);
        CHECK(dos_f2b_claim_point(37, 0) == -1);
        CHECK(dos_f2b_claim_point(0, -1) == -1);
        CHECK(dos_f2b_claim_point(0, 3) == -1);
        CHECK(!dos_f2b_hidden_rect(0, 0, 37, 3));
        CHECK(!dos_f2b_hidden_rect(2, 0, 1, 1));
        CHECK(!dos_f2b_hidden_rect(0, 2, 1, 1));
        CHECK(!dos_f2b_hidden_rect(INT_MIN, INT_MIN, -1, -1));
        raster_pixel(31, 1, 0); reference_raster_pixel(31, 1, 0);
        raster_pixel(31, 1, 99); /* First opaque zero cannot be overwritten. */
        CHECK(dos_f2b_stats.covered == 1);
        CHECK(dos_f2b_claim_point(31, 1) == 0);
        CHECK(dos_f2b_hidden_rect(31, 1, 32, 2) == 1);
        CHECK(dos_f2b_stats.hidden_primitives == 1 && dos_f2b_stats.covered == 1);
        CHECK(!dos_f2b_hidden_rect(30, 1, 32, 2));
        /* An all-zero cutout covers nothing, then opaque texture zero fills
         * every unclaimed pixel. The existing point remains frontmost. */
        rect(v, 0, 0, width, height, 16 * 65536 - 1);
        raster_poly(v, 4, zero_tile, RP_TRANSPARENT);
        CHECK(dos_f2b_stats.covered == 1);
        raster_poly(v, 4, zero_tile, 0);
        reference_raster_poly(v, 4, zero_tile, 0);
        CHECK(dos_f2b_stats.covered == (unsigned)(width * height));
        CHECK(dos_f2b_full());
        CHECK(dos_f2b_hidden_rect(INT_MIN, INT_MIN, INT_MAX, INT_MAX));
        CHECK(!dos_f2b_hidden_rect(37, 0, 99, 1));
        raster_poly(v, 4, tile, 0); raster_flat_poly(v, 4, 91); raster_pixel(0, 0, 99);
        same_image(); guards(actual, 0);
        dos_f2b_end(); CHECK(!dos_f2b_full());
        /* Inactive flat-row helper clips stores to the recorded width. */
        dos_f2b_flat_row(actual + GUARD, -9, 99, 0, s, 61);
        for (x = 0; x < width; ++x) reference_raster_pixel(x, 0, 61);
        same_image(); guards(actual, 0);
        CHECK(dos_f2b_begin(width, height) == 1); CHECK(dos_f2b_stats.covered == 0);
        for (y = 0; y < height; ++y) {
            dos_f2b_flat_row(actual + GUARD + y * pitch * s, -10, 99, y, s, 0);
            for (x = 0; x < width; ++x) reference_raster_pixel(x, y, 0);
        }
        CHECK(dos_f2b_full()); same_image(); guards(actual, 0);
        CHECK(dos_f2b_begin(width, height) == 1);
        CHECK(!dos_f2b_full() && !dos_f2b_hidden_rect(0, 0, width, height));
        dos_f2b_end();
    }
    invalid_traps();
    puts("PASS: target/begin bounds, invalid active traps preserve image/advance fields, clipping, opaque zero, hidden rectangles/full/reset");
}

int main(int argc, char **argv)
{
    int i, x, y;
    if (argc != 2) { fputs("Use scenes|traps|phase|bounds\n", stderr); return 2; }
    mode = argv[1];
    allocation = malloc(262144);
    CHECK(allocation != NULL);
    page = (unsigned char *)(((uintptr_t)allocation + 65535) & ~(uintptr_t)65535);
    for (i = 0; i < 131072; ++i) page[i] = i % 5 ? (unsigned char)(i * 71 + (i >> 8) * 13 + 11) : 0;
    tile = page + 0x2010; zero_tile = page + 65536 + 0x4010;
    for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x) zero_tile[y * 256 + x] = 0;
    if (!strcmp(mode, "scenes")) scenes();
    else if (!strcmp(mode, "traps")) traps();
    else if (!strcmp(mode, "phase")) phase();
    else if (!strcmp(mode, "bounds")) bounds();
    else { free(allocation); return 2; }
    free(allocation);
    printf("Checked %u assertions\n", checks);
    return 0;
}
