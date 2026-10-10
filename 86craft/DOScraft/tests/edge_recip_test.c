/* Native PE32 proofs use the actual pinned fmath.h IDIV and raster/trap.S.
 * Only this oracle/harness uses 64-bit arithmetic; the candidate never does.
 * Host execution time is not evidence of DOS/486 performance.
 */
#include "edge_recip.h"

#ifdef EDGE_RECIP_AUDIT
/* Audit the exact shared fast core, including shifting and sign restoration.
 * Inputs obey the production gate; the fallback is deliberately excluded.
 */
int audit_fast4(uint32_t magnitude, uint32_t dy, int negative)
{
    uint32_t q = dos_edge_recip_quotient(magnitude << 4, dy);
    return negative ? -(int)q : (int)q;
}
int audit_fast16(uint32_t magnitude, uint32_t dy, int negative)
{
    uint32_t q = dos_edge_recip_quotient(magnitude << 16, dy);
    return negative ? -(int)q : (int)q;
}
int audit_full4(int value, int dy)
{
    return dos_edge_recip_divshift(value, dy, 4);
}
int audit_full16(int value, int dy)
{
    return dos_edge_recip_divshift(value, dy, 16);
}
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raster.h"
void reference_raster_set_target(u8 *, int, int, int, int);
void reference_raster_poly(const RVert *, int, const u8 *, int);
void reference_raster_flat_poly(const RVert *, int, u8);
void reference_raster_finish(void);
extern int reference_g_fastFlat;
extern u32 reference_g_statPixels;

static uint32_t random_state = 0x472918adu;
static unsigned long checks, fast_checks, fallback_checks, corrections;

static uint32_t random32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

/* Keep IDIV and candidate at separate call boundaries, including dynamic s. */
static __attribute__((noinline)) int original_div(int a, int b, int s)
{
    return divshift(a, b, s);
}
static __attribute__((noinline)) int candidate_div(int a, int b, int s)
{
    return dos_edge_recip_divshift(a, b, s);
}
static __attribute__((noinline)) int candidate4(int a, int b)
{
    return dos_edge_recip_divshift(a, b, 4);
}
static __attribute__((noinline)) int candidate16(int a, int b)
{
    return dos_edge_recip_divshift(a, b, 16);
}

static void require(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}

static void check_division(int value, int dy, int shift)
{
    int64_t expected, numerator;
    uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int reference, actual, constant;
    require(dy != 0, "Harness must never pass a zero divisor");
    numerator = (int64_t)value * ((int64_t)1 << shift);
    expected = numerator / dy;
    /* Filter only oracle overflow: the original IDIV explicitly forbids it. */
    if (expected < INT_MIN || expected > INT_MAX) return;
    reference = original_div(value, dy, shift);
    actual = candidate_div(value, dy, shift);
    constant = shift == 4 ? candidate4(value, dy) :
               shift == 16 ? candidate16(value, dy) : actual;
    if (reference != expected || actual != reference || constant != reference) {
        fprintf(stderr, "division mismatch a=%d b=%d s=%d expected=%lld "
                "original=%d candidate=%d constant=%d\n", value, dy, shift,
                (long long)expected, reference, actual, constant);
        exit(1);
    }
    ++checks;
    if ((shift == 4 || shift == 16) && dy >= 1 && dy <= 8192 &&
        magnitude <= ((uint32_t)INT_MAX >> shift)) {
        ++fast_checks;
        if (dy > 1) {
            uint32_t n = magnitude << shift;
            uint32_t q = (uint32_t)(((uint64_t)n * g_dos_edge_recip[dy]) >> 32);
            uint32_t exact = n / (uint32_t)dy;
            uint64_t product = (uint64_t)q * (uint32_t)dy;
            require(q == exact || q == exact + 1u, "Estimate exceeds one quotient");
            require(product <= UINT32_MAX, "Correction product overflows 32 bits");
            require(product <= (uint64_t)n + (uint32_t)dy,
                    "Correction product exceeds n+d");
            if (q != exact) ++corrections;
        }
    } else ++fallback_checks;
}

static void signed_pair(uint32_t magnitude, int dy, int shift)
{
    require(magnitude <= INT_MAX, "signed_pair magnitude exceeds INT_MAX");
    check_division((int)magnitude, dy, shift);
    check_division(-(int)magnitude, dy, shift);
}

static int divisions(void)
{
    int dy, shift, j;
    uint32_t table[8193];
    require(sizeof(int) == 4 && sizeof(void *) == 4, "Expected native 32-bit x86 ABI");
    dos_edge_recip_init();
    require(g_dos_edge_recip[0] == 0 && g_dos_edge_recip[1] == 0,
            "Unused table entries must remain zero");
    for (dy = 2; dy <= 8192; ++dy)
        require(g_dos_edge_recip[dy] ==
                (uint32_t)((((uint64_t)1 << 32) + dy - 1) / dy),
                "Table reciprocal is not ceil(2^32/d)");
    memcpy(table, g_dos_edge_recip, sizeof(table));
    for (j = 0; j < 8; ++j) dos_edge_recip_init();
    require(!memcmp(table, g_dos_edge_recip, sizeof(table)), "Init changed table");
    /* Prove subsequent init calls do not rebuild the table. */
    g_dos_edge_recip[8192] ^= 1u;
    dos_edge_recip_init();
    require(g_dos_edge_recip[8192] == (table[8192] ^ 1u), "Init rebuilt table");
    g_dos_edge_recip[8192] = table[8192];

    for (shift = 4; shift <= 16; shift += 12) {
        uint32_t limit = (uint32_t)INT_MAX >> shift;
        for (dy = 1; dy <= 8192; ++dy) {
            /* Every divisor, both signs, every small magnitude 0..128. */
            for (j = 0; j <= 128; ++j) signed_pair((uint32_t)j, dy, shift);
            signed_pair(limit - 1, dy, shift);
            signed_pair(limit, dy, shift);
            signed_pair(limit + 1, dy, shift);
            for (j = 0; j < 32; ++j)
                signed_pair(random32() % (limit + 1u), dy, shift);
            /* Quotient-transition neighbors; correction is exercised near max. */
            for (j = 0; j < 8; ++j) {
                uint32_t k = (limit << shift) / (uint32_t)dy;
                uint32_t center;
                if (k >= (uint32_t)j) k -= (uint32_t)j;
                center = (uint32_t)(((uint64_t)k * (uint32_t)dy) >> shift);
                if (center) signed_pair(center - 1, dy, shift);
                signed_pair(center, dy, shift);
                if (center < limit) signed_pair(center + 1, dy, shift);
            }
        }
        for (j = 0; j < 250000; ++j) {
            uint32_t magnitude = random32() % (limit + 1u);
            dy = 1 + random32() % 8192;
            check_division((j & 1) ? -(int)magnitude : (int)magnitude, dy, shift);
        }
        /* Full shift-16 fast magnitude domain for representative hard divisors. */
        if (shift == 16) {
            const int divisors[] = {1, 2, 3, 7, 31, 255, 4095, 8191, 8192};
            unsigned int k;
            for (k = 0; k < sizeof(divisors) / sizeof(divisors[0]); ++k)
                for (j = 0; (uint32_t)j <= limit; ++j)
                    signed_pair((uint32_t)j, divisors[k], shift);
        }
    }
    {
        const int divisors[] = {1, 2, 3, 8191, 8192, 8193, 16384, 65536,
                               INT_MAX, -1, -2, -3, -8192, -8193, -65536, INT_MIN};
        const int values[] = {0, 1, -1, 32766, -32766, 32767, -32767,
                             32768, -32768, 134217726, -134217726,
                             134217727, -134217727, 134217728, -134217728,
                             INT_MAX, INT_MIN, INT_MIN + 1};
        unsigned int a, b;
        for (a = 0; a < sizeof(values) / sizeof(values[0]); ++a)
            for (b = 0; b < sizeof(divisors) / sizeof(divisors[0]); ++b) {
                check_division(values[a], divisors[b], 4);
                check_division(values[a], divisors[b], 16);
            }
        /* Other supported original shift values must always fall back. */
        for (a = 0; a < sizeof(values) / sizeof(values[0]); ++a)
            for (b = 0; b < sizeof(divisors) / sizeof(divisors[0]); ++b) {
                check_division(values[a], divisors[b], 1);
                check_division(values[a], divisors[b], 8);
                check_division(values[a], divisors[b], 31);
            }
        for (j = 0; j < 250000; ++j) {
            int value = (int)random32();
            int divisor = (int)random32();
            if (!divisor) divisor = 1;
            check_division(value, divisor, (j & 1) ? 4 : 16);
        }
    }
    require(fast_checks > 5000000 && fallback_checks > 200000 && corrections > 0,
            "Insufficient fast/fallback/correction coverage");
    printf("PASS: %lu exact original-IDIV divisions (%lu fast, %lu fallback, "
           "%lu corrected estimates); all 8192 divisors, signed boundaries\n",
           checks, fast_checks, fallback_checks, corrections);
    return 0;
}

static int guarded_parity(void)
{
    /* Aligned atlas storage; a tile stays inside its 4096-byte page. */
    unsigned char *allocation = malloc(65536);
    unsigned char *tile;
    static unsigned char a[512 * 240 + 32], b[sizeof(a)];
    int scale, i, j, row, x;
    unsigned long negative_u_edges = 0, negative_v_edges = 0;
    unsigned long fast_edges = 0, fallback_edges = 0;
    require(allocation != NULL, "Texture allocation failed");
    tile = (unsigned char *)(((uintptr_t)allocation + 4095) & ~(uintptr_t)4095);
    for (i = 0; i < 65536; ++i)
        allocation[i] = i % 7 ? (unsigned char)(i * 43 + 17) : 0;
    for (scale = 1; scale <= 2; ++scale) for (i = 0; i < 4096; ++i) {
        const int heights[] = {1, 7, 8, 15, 16, 17, 31, 255, 4095, 8191, 8192, 8193};
        int w = 320 / scale, h = 200 / scale, flags = i % 4;
        int left = (int)(random32() % (w * 16 + 2048)) - 1024;
        int top = (int)(random32() % (h * 16 + 1600)) - 800;
        int width = 1 + random32() % (w * 16 + 1024);
        int height = i % 3 ? 1 + random32() % (h * 16 + 1600) : heights[(i / 3) % 12];
        int skew = (int)(random32() % 1025) - 512;
        int umax = flags & RP_WRAP ? 16 * (1 + random32() % 15) * 65536 - 1 : 16 * 65536 - 1;
        int u0 = i & 1 ? umax : 0, u1 = i & 1 ? 0 : umax;
        int v0 = i & 2 ? umax : 0, v1 = i & 2 ? 0 : umax;
        RVert v[4];
        if (i % 16 == 0) { top = -16; height = 8192; skew = 32767; }
        if (i % 16 == 1) { top = -16; height = 8193; skew = -32768; }
        if (i % 16 == 2) { top = -16; height = 8192; skew = 32768; }
        /* Affine rectangles/parallelograms stay strictly convex, including
         * subpixel heights, either winding and zero/negative UV gradients. */
        v[0] = (RVert){left, top, u0, v0};
        v[1] = (RVert){left + width, top, u1, v0};
        v[2] = (RVert){left + width + skew, top + height, u1, v1};
        v[3] = (RVert){left + skew, top + height, u0, v1};
        if (i & 16) for (j = 0; j < 4; ++j) {
            int t = v[j].u; v[j].u = v[j].v; v[j].v = t;
        }
        if (i & 4) { RVert t = v[1]; v[1] = v[3]; v[3] = t; }
        for (j = 0; j < 4; ++j) {
            const RVert *p = &v[j], *q = &v[(j + 1) % 4];
            int dy = q->y - p->y, delta = q->x - p->x;
            uint32_t mag = delta < 0 ? 0u - (uint32_t)delta : (uint32_t)delta;
            if (dy) {
                int du = q->u - p->u, dv = q->v - p->v;
                if (dy < 0) { dy = -dy; du = -du; dv = -dv; }
                if (du < 0) ++negative_u_edges;
                if (dv < 0) ++negative_v_edges;
                if (dy <= 8192 && mag <= 32767) ++fast_edges;
                else ++fallback_edges;
            }
        }
        memset(a, 197, sizeof(a)); memcpy(b, a, sizeof(a));
        reference_raster_set_target(a + 16, w, h, 512, scale);
        raster_set_target(b + 16, w, h, 512, scale);
        reference_g_fastFlat = g_fastFlat = (i >> 3) & 1;
        reference_g_statPixels = g_statPixels = 0;
        if (i % 5 == 0) {
            reference_raster_flat_poly(v, 4, (u8)i);
            raster_flat_poly(v, 4, (u8)i);
        } else {
            reference_raster_poly(v, 4, tile, flags);
            raster_poly(v, 4, tile, flags);
        }
        reference_raster_finish(); raster_finish();
        if (memcmp(a, b, sizeof(a)) || reference_g_statPixels != g_statPixels) {
            fprintf(stderr, "guarded polygon mismatch case=%d scale=%d flags=%d\n",
                    i, scale, flags); free(allocation); return 1;
        }
        /* Independently check every byte outside the actual output rectangle,
         * including both scale-2 rows, pitch padding and unused/HUD rows. */
        for (j = 0; j < 16; ++j) require(a[j] == 197 && b[j] == 197, "Leading guard changed");
        for (row = 0; row < 240; ++row) for (x = 0; x < 512; ++x)
            if (row >= h * scale || x >= w * scale)
                require(a[16 + row * 512 + x] == 197 && b[16 + row * 512 + x] == 197,
                        "Pitch padding/HUD/unused row changed");
        for (j = 512 * 240 + 16; j < (int)sizeof(a); ++j)
            require(a[j] == 197 && b[j] == 197, "Trailing guard changed");
    }
    require(negative_u_edges && negative_v_edges && fast_edges && fallback_edges,
            "Polygon coverage missing");
    free(allocation);
    puts("PASS: 8192 convex polygons, clipped framebuffer/pitch/HUD/guards/stats, "
         "negative UV gradients and fast/fallback edge boundaries");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "division")) return divisions();
    if (!strcmp(argv[1], "guards")) return guarded_parity();
    return 2;
}
#endif
