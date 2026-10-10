/* Native 32-bit correctness only. The independent framebuffer oracle is the
 * actual checksum-pinned trap.S, staged mechanically by build_imported.
 * No production renderer integration or host/486 speed claim. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raster.h"
#include "f2b_texel.h"

/* Exact private raster.c layout; official abi_check.c checks the original too. */
typedef struct {
    uint8_t *row;
    int stride, rows;
    int lx, ldx, lu, ldu, lv, ldv;
    int rx, rdx, ru, rdu, rv, rdv;
    const uint8_t *tile;
    int maxX;
    uint32_t pixels;
} Trap;
#define OFFSET(field, value) \
    typedef char offset_##field[(offsetof(Trap, field) == (value)) ? 1 : -1]
OFFSET(row, 0); OFFSET(stride, 4); OFFSET(rows, 8);
OFFSET(lx, 12); OFFSET(ldx, 16); OFFSET(lu, 20); OFFSET(ldu, 24);
OFFSET(lv, 28); OFFSET(ldv, 32); OFFSET(rx, 36); OFFSET(rdx, 40);
OFFSET(ru, 44); OFFSET(rdu, 48); OFFSET(rv, 52); OFFSET(rdv, 56);
OFFSET(tile, 60); OFFSET(maxX, 64); OFFSET(pixels, 68);
typedef char trap_size[(sizeof(Trap) == 72) ? 1 : -1];
typedef char actual_pointer32[(sizeof(uintptr_t) == 4) ? 1 : -1];
extern int g_recip14[321];
void trap_opaque1(Trap *); void trap_opaque2(Trap *);
void trap_transp1(Trap *); void trap_transp2(Trap *);
void trap_opaque1w(Trap *); void trap_opaque2w(Trap *);
void trap_transp1w(Trap *); void trap_transp2w(Trap *);
static void (*const traps[8])(Trap *) = {
    trap_opaque1, trap_opaque2, trap_transp1, trap_transp2,
    trap_opaque1w, trap_opaque2w, trap_transp1w, trap_transp2w
};

#define ATLAS_BYTES 196608u
#define FB_BYTES 704
#define ROW_OFFSET 17 /* Deliberately odd physical destination, even at scale 2. */
static uint8_t reference[FB_BYTES], sparse[FB_BYTES], expected[FB_BYTES];
static uint8_t *allocation, *aligned;
static unsigned int opaque_zeros, cutout_zeros, clipped_rows, rollover_rows;

static uintptr_t address(const struct DosF2BTexel *state)
{
    return state->base + ((uintptr_t)state->v_index << 8) + state->u_index;
}

static void allocate_atlas(void)
{
    uint32_t i;
    allocation = malloc(ATLAS_BYTES);
    assert(allocation);
    aligned = (uint8_t *)(((uintptr_t)allocation + 65535u) & ~(uintptr_t)65535u);
    /* Two complete 64KB windows cover byte rollover even when the initial
     * tile+coordinate addition crosses the first window boundary. */
    assert(aligned >= allocation && aligned + 131072u <= allocation + ATLAS_BYTES);
    for (i = 0; i < ATLAS_BYTES; ++i) {
        uint32_t hash = i * 0x45d9f3bu;
        hash ^= hash >> 16;
        allocation[i] = i % 5u ? (uint8_t)(1u + hash % 255u) : 0;
    }
    assert(((uintptr_t)aligned & 65535u) == 0);
}

/* These are original trap.S row setup operations, not the jump formula.
 * Unsigned arithmetic models 32-bit wrapping SUB/ADD/IMUL without C UB. */
static int32_t add32(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}
static int32_t sub32(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a - (uint32_t)b);
}
static int32_t mul32(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

static void row_setup(const Trap *t, int *left, int *width,
                      int32_t *u, int32_t *v, int32_t *du, int32_t *dv)
{
    int xl = add32(t->lx, 0x7fff) >> 16;
    int xr = add32(t->rx, 0x7fff) >> 16;
    int full_width = add32(sub32(t->rx, t->lx), 0x8000) >> 16;
    if (full_width < 1) full_width = 1;
    if (full_width > 320) full_width = 320;
    if (xr > t->maxX) xr = t->maxX;
    *left = xl < 0 ? 0 : xl;
    *width = xr - *left;
    *du = mul32(sub32(t->ru, t->lu) >> 8, g_recip14[full_width]) >> 6;
    *dv = mul32(sub32(t->rv, t->lv) >> 8, g_recip14[full_width]) >> 6;
    *u = t->lu; *v = t->lv;
    if (xl < 0) {
        int32_t pre = sub32(add32((int32_t)((uint32_t)*left << 16), 0x8000), t->lx) >> 8;
        *u = add32(*u, mul32(*du >> 8, pre));
        *v = add32(*v, mul32(*dv >> 8, pre));
    }
}

static int uncovered(int x, int width, int profile)
{
    switch (profile & 3) {
    case 0: return 1; /* Also verify the entire original row. */
    case 1: return x & 1;
    case 2: return x >= width / 2 && (x < width / 2 + 3 || x == width - 1);
    default: return (x % 23) >= 7 && (x % 23) < 13;
    }
}

static void compare_row(int variant, int width, int profile)
{
    struct DosF2BTexel state;
    Trap t = {0}, original;
    int scale = 1 + (variant & 1), wrap = variant >= 4;
    int transparent = (variant & 2) != 0;
    int left, visible, x, current = 0, k, fb;
    int32_t u, v, du, dv;
    uintptr_t initial_base;
    t.row = reference + ROW_OFFSET;
    assert(((uintptr_t)t.row & 1u) != 0);
    t.stride = 647; t.rows = 1; t.maxX = 320;
    /* Full row width always 1..320, with fractional edges and clipping.
     * Profile 3 forces a different visible width without left clipping. */
    t.lx = (profile == 1 || profile == 5) ? -7 * 65536 + 0x2100 :
           (profile == 2 || profile == 6) ? -19 * 65536 - 0x3900 :
           (profile == 3 || profile == 7) ? 311 * 65536 + 0x1300 : 0x1700;
    t.rx = t.lx + width * 65536;
    t.lu = (profile & 1) ? -13 * 65536 + 0xffef : 14 * 65536 + 0x1234;
    t.lv = (profile & 2) ? 37 * 65536 + 0xabcd : -2 * 65536 + 0xfffb;
    /* Safe 31-bit assembly gradient products even at width 1; negative and
     * positive, fractional carries, wrap crossings and non-wrap rollovers. */
    t.ru = t.lu + ((profile & 1) ? -121 * 65536 - 0x3200 : 79 * 65536 + 0x7400);
    t.rv = t.lv + ((profile & 2) ? -97 * 65536 - 0x5300 : 111 * 65536 + 0x1900);
    if (profile == 4) { t.ru = t.lu; t.rv = t.lv; }
    if (profile == 7) { t.ru = t.lu - 256; t.rv = t.lv + 256; }
    /* All tiles are 16-byte aligned inside 4KB pages. High-page offsets
     * exercise both independent address bytes and preserved pointer high16. */
    t.tile = aligned + (profile == 6 ? 65520 :
                         (profile & 4) ? 61440 + 240 : 8192 + 112);
    assert(((uintptr_t)t.tile & 15u) == 0);
    original = t;
    row_setup(&original, &left, &visible, &u, &v, &du, &dv);
    memset(reference, 197, sizeof(reference));
    memcpy(sparse, reference, sizeof(sparse));
    traps[variant](&t); /* Actual pinned assembly is the framebuffer oracle. */
    assert(t.rows == 0 && t.row == original.row + original.stride);
    assert(t.pixels == (uint32_t)(visible > 0 ? visible : 0));
    memcpy(expected, reference, sizeof(expected));
    if (visible <= 0) {
        assert(memcmp(sparse, reference, sizeof(reference)) == 0);
        return;
    }
    if (left == 0 && original.lx < 0) ++clipped_rows;
    assert(dos_f2b_texel_init(&state, original.tile, u, v, du, dv, wrap) == 1);
    initial_base = state.base;
    for (x = 0; x < visible; ++x) {
        fb = ROW_OFFSET + (left + x) * scale;
        if (!uncovered(x, visible, profile)) {
            for (k = 0; k < scale; ++k) expected[fb + k] = 197;
            continue;
        }
        /* Jump directly over each covered gap, then advance once per sample.
         * No new gradient or prestep is computed for an uncovered run. */
        assert(dos_f2b_texel_jump(&state, x - current) == 1);
        assert(state.base == initial_base);
        assert(address(&state) >= (uintptr_t)allocation &&
               address(&state) < (uintptr_t)(allocation + ATLAS_BYTES));
        {
            uint8_t texel = *(const uint8_t *)address(&state);
            if (texel || !transparent)
                for (k = 0; k < scale; ++k) sparse[fb + k] = texel;
            if (!texel) {
                if (transparent) ++cutout_zeros;
                else ++opaque_zeros;
            }
        }
        if (!wrap && (state.u_index > 239 || state.v_index > 239)) ++rollover_rows;
        assert(dos_f2b_texel_jump(&state, 1) == 1);
        current = x + 1;
    }
    assert(dos_f2b_texel_jump(&state, visible - current) == 1);
    if (memcmp(sparse, expected, sizeof(sparse))) {
        fprintf(stderr, "trap mismatch variant=%d width=%d profile=%d visible=%d\n",
                variant, width, profile, visible);
        abort();
    }
    /* Independently check physical framebuffer guards, including right clip. */
    for (x = 0; x < FB_BYTES; ++x)
        if (x < ROW_OFFSET + left * scale || x >= ROW_OFFSET + (left + visible) * scale)
            assert(reference[x] == 197 && sparse[x] == 197);
}

static void test_parity(void)
{
    int variant, width, profile;
    allocate_atlas();
    /* The actual original raster object owns and initializes this table. */
    raster_set_target(reference + ROW_OFFSET, 320, 1, 647, 1);
    assert(g_recip14[1] == 16384 && g_recip14[320] == 51);
    for (variant = 0; variant < 8; ++variant)
        for (width = 1; width <= 320; ++width)
            for (profile = 0; profile < 8; ++profile) compare_row(variant, width, profile);
    assert(opaque_zeros && cutout_zeros && clipped_rows && rollover_rows);
    free(allocation);
    puts("PASS: 20480 pinned trap rows; all 8 variants, widths 1..320, sparse gaps, clipping, negative slopes, wrap/byte rollover, cutout/opaque zero and unaligned framebuffer guards");
}

/* Independent one-texel oracle: actual x86 ADD/ADCb/ADDw/ADCb, with packed
 * ECX fractions and BL/BH address bytes. No n*step jump formula here. */
struct ByteOracle { uint32_t fractions, index, du_fraction, dv_fraction, steps; };

static struct ByteOracle byte_init(uintptr_t tile, int32_t u, int32_t v,
                                    int32_t du, int32_t dv, int wrap)
{
    struct ByteOracle o;
    o.fractions = ((uint32_t)u << 16) | ((uint32_t)v & 65535u);
    o.index = (((uint32_t)v >> 8) & 0x0f00u) | (((uint32_t)u >> 16) & 15u);
    if (!wrap) o.index += (uint32_t)tile;
    o.du_fraction = (uint32_t)du << 16;
    o.dv_fraction = (uint32_t)dv & 65535u;
    o.steps = ((uint32_t)(du >> 16) & 255u) | (((uint32_t)(dv >> 16) & 255u) << 8);
    return o;
}

static void byte_step(struct ByteOracle *o, int wrap)
{
    __asm__ volatile (
        "addl %[du], %[frac]\n\t"
        "adcb %b[steps], %b[index]\n\t"
        "addw %w[dv], %w[frac]\n\t"
        "adcb %h[steps], %h[index]"
        : [frac] "+&c" (o->fractions), [index] "+&b" (o->index)
        : [du] "S" (o->du_fraction), [dv] "a" (o->dv_fraction), [steps] "d" (o->steps)
        : "cc");
    if (wrap) o->index &= 0x0f0fu;
}

static void check_phase(const struct DosF2BTexel *s, const struct ByteOracle *o,
                         uintptr_t tile, int wrap)
{
    assert(s->u_fraction == o->fractions >> 16);
    assert(s->v_fraction == (o->fractions & 65535u));
    assert(s->u_index == (o->index & 255u));
    assert(s->v_index == ((o->index >> 8) & 255u));
    assert(s->u_step == (o->steps & 255u) && s->v_step == (o->steps >> 8));
    assert(s->du_fraction == o->du_fraction >> 16 && s->dv_fraction == o->dv_fraction);
    assert(s->wrap == wrap);
    assert(s->base == (wrap ? tile : (uintptr_t)(o->index & 0xffff0000u)));
    assert(address(s) == (wrap ? tile + o->index : o->index));
}

static void test_carry(void)
{
    static const int32_t gradients[] = {
        INT32_MIN, INT32_MIN + 65535, -16777217, -16777216,
        -65537, -65536, -1, 0, 1, 65535, 65536, 65537,
        16777215, 16777216, 16777217, INT32_MAX
    };
    static const int32_t phases[] = {INT32_MIN, -65537, -1, 0, 65535, INT32_MAX};
    static const uintptr_t tiles[] = {0x12340000u, 0x12347ff0u, 0x1234fff0u};
    unsigned int i, j, p, t;
    int wrap, n;
    unsigned int roll_u = 0, roll_v = 0;
    {
        struct DosF2BTexel s;
        /* Initial pointer addition carries into high16; subsequent byte
         * carries must preserve that NEW high16, not the tile's high16. */
        assert(dos_f2b_texel_init(&s, (const uint8_t *)0x1234fff0u,
                                -1, -1, 65536, 65536, 0) == 1);
        assert(s.base == 0x12350000u && s.u_index == 255 && s.v_index == 14);
        assert(address(&s) == 0x12350effu);
        assert(dos_f2b_texel_jump(&s, 1) == 1);
        assert(s.base == 0x12350000u && s.u_index == 0 && s.v_index == 15);
        assert(address(&s) == 0x12350f00u); /* BL carry never increments BH. */
        assert(dos_f2b_texel_init(&s, (const uint8_t *)0x1234fff0u,
                                -1, -1, -65537, -131073, 0) == 1);
        assert(dos_f2b_texel_jump(&s, 1) == 1);
        assert(s.base == 0x12350000u && s.u_index == 254 && s.v_index == 12);
        assert(s.u_fraction == 65534 && s.v_fraction == 65534);
    }
    /* Synthetic pointers are intentionally never dereferenced by the core.
     * Every jump 0..320 is compared to one further real byte carry step. */
    for (wrap = 0; wrap <= 1; ++wrap)
        for (t = 0; t < sizeof(tiles) / sizeof(tiles[0]); ++t)
            for (p = 0; p < sizeof(phases) / sizeof(phases[0]); ++p)
                for (i = 0; i < sizeof(gradients) / sizeof(gradients[0]); ++i) {
                    struct DosF2BTexel initial, jumped, split;
                    int32_t du = gradients[i], dv = gradients[(i * 7 + p) % 16];
                    struct ByteOracle o = byte_init(tiles[t], phases[p], phases[5 - p], du, dv, wrap);
                    memset(&initial, 0xa5, sizeof(initial));
                    assert(dos_f2b_texel_init(&initial, (const uint8_t *)tiles[t],
                           phases[p], phases[5 - p], du, dv, wrap) == 1);
                    check_phase(&initial, &o, tiles[t], wrap);
                    for (n = 0; n <= 320; ++n) {
                        memcpy(&jumped, &initial, sizeof(jumped));
                        assert(dos_f2b_texel_jump(&jumped, n) == 1);
                        check_phase(&jumped, &o, tiles[t], wrap);
                        assert(jumped.base == initial.base);
                        /* Composition after a nonzero prior phase, not just
                         * jumps from initialization. Parts sum to n. */
                        memcpy(&split, &initial, sizeof(split));
                        assert(dos_f2b_texel_jump(&split, n / 3) == 1);
                        assert(dos_f2b_texel_jump(&split, n - n / 3) == 1);
                        assert(memcmp(&split, &jumped, sizeof(split)) == 0);
                        j = o.index;
                        byte_step(&o, wrap);
                        if ((o.index & 255u) < (j & 255u)) ++roll_u;
                        if ((o.index & 0xff00u) < (j & 0xff00u)) ++roll_v;
                    }
                }
    assert(roll_u && roll_v);
    puts("PASS: 0..320 jumps and composition against repeated x86 byte ADD/ADC; initial address carry, negative/large gradients, extreme fractions, independent byte rollover and fixed pointer high16");
}

static void test_invalid(void)
{
    static const int32_t bad_jumps[] = {INT32_MIN, -1, 321, INT32_MAX};
    static const int bad_wraps[] = {INT_MIN, -1, 2, INT_MAX};
    struct DosF2BTexel state, before;
    unsigned int i;
    int wrap, corruption;
    memset(&state, 0xa5, sizeof(state));
    memcpy(&before, &state, sizeof(before));
    assert(dos_f2b_texel_init(NULL, (const uint8_t *)0x12340000u, 0, 0, 0, 0, 0) == -1);
    assert(dos_f2b_texel_init(&state, NULL, 0, 0, 0, 0, 0) == -1);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    for (i = 0; i < sizeof(bad_wraps) / sizeof(bad_wraps[0]); ++i) {
        assert(dos_f2b_texel_init(&state, (const uint8_t *)0x12340000u,
                                0, 0, 0, 0, bad_wraps[i]) == -1);
        assert(memcmp(&state, &before, sizeof(state)) == 0);
    }
    assert(dos_f2b_texel_jump(NULL, 0) == -1);
    assert(dos_f2b_texel_jump(NULL, 320) == -1);
    for (wrap = 0; wrap <= 1; ++wrap) {
        assert(dos_f2b_texel_init(&state, (const uint8_t *)0x1234fff0u,
                                -1, INT32_MAX, -65537, 65535, wrap) == 1);
        assert(dos_f2b_texel_jump(&state, 29) == 1);
        memcpy(&before, &state, sizeof(before));
        for (i = 0; i < sizeof(bad_jumps) / sizeof(bad_jumps[0]); ++i) {
            assert(dos_f2b_texel_jump(&state, bad_jumps[i]) == -1);
            assert(memcmp(&state, &before, sizeof(state)) == 0);
        }
        assert(dos_f2b_texel_jump(&state, 0) == 1);
        assert(memcmp(&state, &before, sizeof(state)) == 0);
    }
    for (corruption = 0; corruption < 4; ++corruption) {
        assert(dos_f2b_texel_init(&state, (const uint8_t *)0x12340000u,
                                0, 0, 0, 0, corruption != 3) == 1);
        switch (corruption) {
        case 0: state.wrap = 2; break;
        case 1: state.u_index = 16; break;
        case 2: state.v_index = 255; break;
        default: state.base |= 1; break;
        }
        memcpy(&before, &state, sizeof(before));
        assert(dos_f2b_texel_jump(&state, 0) == -1);
        assert(dos_f2b_texel_jump(&state, 1) == -1);
        assert(memcmp(&state, &before, sizeof(state)) == 0);
    }
    puts("PASS: invalid init/jump arguments and corrupted mode/indices/base preserve every state byte; zero jump is a no-op");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "parity")) test_parity();
    else if (!strcmp(argv[1], "carry")) test_carry();
    else if (!strcmp(argv[1], "invalid")) test_invalid();
    else return 2;
    return 0;
}
