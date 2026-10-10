#include <limits.h>
#include <stddef.h>
#include <string.h>
#include "coverage_spans.h"
#include "f2b_texel.h"
#include "f2b_raster.h"

#define CHECK_OFFSET(field, offset) \
    typedef char f2b_offset_##field[(offsetof(struct DosF2BTrap, field) == offset) ? 1 : -1]
CHECK_OFFSET(row, 0); CHECK_OFFSET(stride, 4); CHECK_OFFSET(rows, 8);
CHECK_OFFSET(lx, 12); CHECK_OFFSET(ldx, 16); CHECK_OFFSET(lu, 20);
CHECK_OFFSET(ldu, 24); CHECK_OFFSET(lv, 28); CHECK_OFFSET(ldv, 32);
CHECK_OFFSET(rx, 36); CHECK_OFFSET(rdx, 40); CHECK_OFFSET(ru, 44);
CHECK_OFFSET(rdu, 48); CHECK_OFFSET(rv, 52); CHECK_OFFSET(rdv, 56);
CHECK_OFFSET(tile, 60); CHECK_OFFSET(maxX, 64); CHECK_OFFSET(pixels, 68);
typedef char f2b_trap_size[(sizeof(struct DosF2BTrap) == 72) ? 1 : -1];
typedef char f2b_int_size[(sizeof(int) == 4 && sizeof(unsigned) == 4) ? 1 : -1];
#undef CHECK_OFFSET

extern int g_recip14[321];
extern void dos_f2b_opaque_span(unsigned char *dst, int count,
                              struct DosF2BTexel *state, int scale);
/* These offsets are consumed by f2b_span.S, independently of Trap. */
typedef char phase_base[(offsetof(struct DosF2BTexel, base) == 0) ? 1 : -1];
typedef char phase_u[(offsetof(struct DosF2BTexel, u_fraction) == 4) ? 1 : -1];
typedef char phase_v[(offsetof(struct DosF2BTexel, v_fraction) == 6) ? 1 : -1];
typedef char phase_du[(offsetof(struct DosF2BTexel, du_fraction) == 8) ? 1 : -1];
typedef char phase_dv[(offsetof(struct DosF2BTexel, dv_fraction) == 10) ? 1 : -1];
typedef char phase_ui[(offsetof(struct DosF2BTexel, u_index) == 12) ? 1 : -1];
typedef char phase_vi[(offsetof(struct DosF2BTexel, v_index) == 13) ? 1 : -1];
typedef char phase_us[(offsetof(struct DosF2BTexel, u_step) == 14) ? 1 : -1];
typedef char phase_vs[(offsetof(struct DosF2BTexel, v_step) == 15) ? 1 : -1];
typedef char phase_wrap[(offsetof(struct DosF2BTexel, wrap) == 16) ? 1 : -1];
int dos_f2b_active;
struct DosF2BStats dos_f2b_stats;
static struct DosCoverageSpans coverage;
static unsigned char *target;
static int target_w, target_h, target_scale, target_stride, target_shift;

#ifdef DOS_F2B_FAST_COVERAGE
/* Private hot path: begin has validated state; callers clip spans first.
 * Keep the checked public kernel as the independently selectable reference.
 * Counts advance ONLY on proven first opaque writes, including point writes.
 * A complete row permits constant-time rejection, but never claims a bbox. */
static unsigned short row_claimed[DOS_COVERAGE_MAX_HEIGHT];

static inline unsigned coverage_mask(int left, int right)
{ return UINT_MAX << left & UINT_MAX >> (32 - right); }

static inline int coverage_seek(const unsigned *row, int left, int right, int covered)
{
    while (left < right) {
        int base = left & ~31, end = right - base;
        unsigned bits = covered ? row[left >> 5] : ~row[left >> 5];
        if (end > 32) end = 32;
        bits &= coverage_mask(left - base, end);
        if (bits) return base + __builtin_ctz(bits);
        left = base + end;
    }
    return right;
}

static inline int coverage_run(int y, int left, int right, int32_t *a, int32_t *b)
{
    const unsigned *row;
    int start;
    if (y < 0 || y >= coverage.height) return -1;
    if (left >= right || row_claimed[y] == coverage.width) return 0;
    if (!row_claimed[y]) { *a = left; *b = right; return 1; }
    row = coverage.words + y * coverage.row_stride;
    start = coverage_seek(row, left, right, 0);
    if (start == right) return 0;
    *a = start; *b = coverage_seek(row, start, right, 1);
    return 1;
}

static inline void coverage_claim(int y, int left, int right)
{
    unsigned *row = coverage.words + y * coverage.row_stride;
    row_claimed[y] += (unsigned short)(right - left);
    while (left < right) {
        int base = left & ~31, end = right - base;
        if (end > 32) end = 32;
        row[left >> 5] |= coverage_mask(left - base, end);
        left = base + end;
    }
}

static inline int coverage_hidden(int y, int left, int right)
{
    const unsigned *row;
    if (row_claimed[y] == coverage.width) return 1;
    if (!row_claimed[y]) return 0;
    row = coverage.words + y * coverage.row_stride;
    while (left < right) {
        int base = left & ~31, end = right - base;
        if (end > 32) end = 32;
        if (~row[left >> 5] & coverage_mask(left - base, end)) return 0;
        left = base + end;
    }
    return 1;
}
#else
#define coverage_run(y, left, right, a, b) dos_coverage_find_run(&coverage, y, left, right, a, b)
#define coverage_claim(y, left, right) dos_coverage_mark_span(&coverage, y, left, right)
#endif

void dos_f2b_set_target(unsigned char *fb, int w, int h, int pitch, int scale)
{
    unsigned stride;
    target = fb; target_w = w; target_h = h; target_scale = scale;
    target_stride = 0; target_shift = -1;
    if (!fb || w < 1 || w > DOS_COVERAGE_MAX_WIDTH ||
        h < 1 || h > DOS_COVERAGE_MAX_HEIGHT || (scale != 1 && scale != 2) ||
        pitch < w * scale || pitch > INT_MAX / scale) return;
    target_stride = pitch * scale;
    stride = (unsigned)target_stride;
    if (!(stride & (stride - 1))) {
        target_shift = 0;
        while ((stride >>= 1) != 0) ++target_shift;
    }
}

int dos_f2b_begin(int w, int h)
{
    dos_f2b_active = 0;
    memset(&dos_f2b_stats, 0, sizeof(dos_f2b_stats));
    if (!target_stride || w != target_w || h != target_h) return 0;
    if (dos_coverage_reset(&coverage, w, h) != 1) return 0;
#ifdef DOS_F2B_FAST_COVERAGE
    memset(row_claimed, 0, (size_t)h * sizeof(*row_claimed));
#endif
    dos_f2b_active = 1;
    return 1;
}

void dos_f2b_end(void) { dos_f2b_active = 0; }

int dos_f2b_full(void)
{
    return dos_f2b_active && dos_f2b_stats.covered ==
        (unsigned)(coverage.width * coverage.height);
}

int dos_f2b_hidden_rect(int left, int top, int right, int bottom)
{
    int y;
#ifndef DOS_F2B_FAST_COVERAGE
    int32_t a, b;
#endif
    if (!dos_f2b_active) return 0;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > coverage.width) right = coverage.width;
    if (bottom > coverage.height) bottom = coverage.height;
    if (left >= right || top >= bottom) return 0;
    for (y = top; y < bottom; ++y)
#ifdef DOS_F2B_FAST_COVERAGE
        if (!coverage_hidden(y, left, right)) return 0;
#else
        if (dos_coverage_find_run(&coverage, y, left, right, &a, &b) != 0) return 0;
#endif
    ++dos_f2b_stats.hidden_primitives;
    return 1;
}

int dos_f2b_claim_point(int x, int y)
{
    int result;
    if (!dos_f2b_active) return -1;
    result = dos_coverage_mark_pixel(&coverage, x, y);
    if (result == 1) {
        ++dos_f2b_stats.covered;
#ifdef DOS_F2B_FAST_COVERAGE
        ++row_claimed[y];
#endif
    }
    return result;
}

/* Every caller has proved this entire span was uncovered before writing.
 * Cutouts send only consecutive nonzero writes, never their bounding span. */
static void claim_run(int y, int left, int right)
{
    coverage_claim(y, left, right);
    dos_f2b_stats.covered += (unsigned)(right - left);
}

void dos_f2b_flat_row(unsigned char *row, int left, int right, int y,
                      int scale, unsigned char color)
{
    int32_t a, b;
    int width = dos_f2b_active ? coverage.width : target_w;
    if (!row || (scale != 1 && scale != 2) || width < 1 ||
        width > DOS_COVERAGE_MAX_WIDTH) return;
    if (left < 0) left = 0;
    if (right > width) right = width;
    if (left >= right) return;
    if (!dos_f2b_active) {
        memset(row + left * scale, color, (size_t)(right - left) * scale);
        return;
    }
    while (coverage_run(y, left, right, &a, &b) == 1) {
        memset(row + a * scale, color, (size_t)(b - a) * scale);
        claim_run(y, a, b);
        left = b;
    }
}

/* Explicit modulo-2^32 operations reproduce x86 ADD/SUB/IMUL even when
 * an input's high bits overflow signed C. The target uses arithmetic SAR. */
static inline int32_t add32(int32_t a, int32_t b)
{ return (int32_t)((uint32_t)a + (uint32_t)b); }
static inline int32_t sub32(int32_t a, int32_t b)
{ return (int32_t)((uint32_t)a - (uint32_t)b); }
static inline int32_t mul32(int32_t a, int32_t b)
{ return (int32_t)((uint32_t)a * (uint32_t)b); }

/* One division (fallback) or shift per trap, never per scanline/texel. */
static int trap_y(const struct DosF2BTrap *t, int scale)
{
    uintptr_t offset, y;
    if (!target_stride || target_w != coverage.width || target_h != coverage.height ||
        target_scale != scale || t->stride != target_stride ||
        (uintptr_t)t->row < (uintptr_t)target) return -1;
    offset = (uintptr_t)t->row - (uintptr_t)target;
    if (target_shift >= 0) {
        if (offset & ((unsigned)target_stride - 1)) return -1;
        y = offset >> target_shift;
    } else {
        y = offset / (unsigned)target_stride;
        if (offset - y * (unsigned)target_stride) return -1;
    }
    return y < (uintptr_t)coverage.height ? (int)y : -1;
}

/* Inline reference to trap.S's ADD/ADCb/ADDw/ADCb. The v ADD cannot affect
 * u's fraction: ADDl's carry is just u's 16-bit fractional overflow.
 * Address bytes wrap separately, leaving initial no-wrap high16 intact. */
static inline void step_texel(struct DosF2BTexel *s)
{
    unsigned u = (unsigned)s->u_fraction + s->du_fraction;
    unsigned v = (unsigned)s->v_fraction + s->dv_fraction;
    unsigned mask = s->wrap ? 15u : 255u;
    s->u_fraction = (uint16_t)u; s->v_fraction = (uint16_t)v;
    s->u_index = (uint8_t)((s->u_index + s->u_step + (u >> 16)) & mask);
    s->v_index = (uint8_t)((s->v_index + s->v_step + (v >> 16)) & mask);
}

static void draw_trap(struct DosF2BTrap *t, int scale, int transparent, int wrap)
{
    int y = trap_y(t, scale);
    while (t->rows > 0) {
        int xl = add32(t->lx, 0x7fff) >> 16;
        int xr = add32(t->rx, 0x7fff) >> 16;
        int left = xl < 0 ? 0 : xl;
        int32_t a, b;
        ++dos_f2b_stats.rows;
        if (xr > t->maxX) xr = t->maxX;
        if (xr > left) {
            unsigned sampled = 0;
            /* Original pixels counts the original clipped span, including
             * cutout zero and covered pixels. It is NOT our covered count. */
            t->pixels += (unsigned)(xr - left);
            if (y >= 0 && y < coverage.height && left < coverage.width) {
                int right = xr > coverage.width ? coverage.width : xr;
                int found = coverage_run(y, left, right, &a, &b);
                if (found == 0) {
                    ++dos_f2b_stats.hidden_rows;
                    dos_f2b_stats.skipped += (unsigned)(right - left);
                } else if (found == 1) {
                    struct DosF2BTexel state;
                    int width = add32(sub32(t->rx, t->lx), 0x8000) >> 16;
                    int32_t u = t->lu, v = t->lv, du, dv;
                    int phase = left;
                    if (width < 1) width = 1;
                    if (width > 320) width = 320;
                    du = mul32(sub32(t->ru, t->lu) >> 8, g_recip14[width]) >> 6;
                    dv = mul32(sub32(t->rv, t->lv) >> 8, g_recip14[width]) >> 6;
                    if (xl < 0) {
                        int32_t pre = sub32(add32((int32_t)((uint32_t)left << 16),
                                                 0x8000), t->lx) >> 8;
                        u = add32(u, mul32(du >> 8, pre));
                        v = add32(v, mul32(dv >> 8, pre));
                    }
                    if (dos_f2b_texel_init(&state, t->tile, u, v, du, dv, wrap) == 1) {
                        do {
                            int x, opaque_start = -1;
                            unsigned char *dst = t->row + a * scale;
                            if (a > phase) dos_f2b_texel_jump(&state, a - phase);
                            if (!transparent) {
                                dos_f2b_opaque_span(dst, b - a, &state, scale);
                                claim_run(y, a, b);
                            } else for (x = a; x < b; ++x, dst += scale) {
                                unsigned char color = *(const unsigned char *)(state.base +
                                    ((uintptr_t)state.v_index << 8) + state.u_index);
                                step_texel(&state);
                                if (!transparent || color) {
                                    *dst = color;
                                    if (scale == 2) dst[1] = color;
                                    if (opaque_start < 0) opaque_start = x;
                                } else if (opaque_start >= 0) {
                                    claim_run(y, opaque_start, x);
                                    opaque_start = -1;
                                }
                            }
                            sampled += (unsigned)(b - a);
                            if (opaque_start >= 0) claim_run(y, opaque_start, b);
                            phase = b;
                        } while (b < right && coverage_run(y, b, right, &a, &b) == 1);
                        dos_f2b_stats.sampled += sampled;
                        dos_f2b_stats.skipped += (unsigned)(right - left) - sampled;
                    }
                }
            }
        }
        t->lx = add32(t->lx, t->ldx); t->lu = add32(t->lu, t->ldu);
        t->lv = add32(t->lv, t->ldv); t->rx = add32(t->rx, t->rdx);
        t->ru = add32(t->ru, t->rdu); t->rv = add32(t->rv, t->rdv);
        t->row = (unsigned char *)((uintptr_t)t->row + (uint32_t)t->stride);
        --t->rows;
        /* Preserve invalid initial target status across every row. */
        if (y >= 0) ++y;
    }
}

#define WRAPPER(name, scale, transparent, wrap) \
    extern void __real_##name(struct DosF2BTrap *t); \
    void __wrap_##name(struct DosF2BTrap *t) { \
        if (!dos_f2b_active) __real_##name(t); \
        else if (t) draw_trap(t, scale, transparent, wrap); \
    }
WRAPPER(trap_opaque1, 1, 0, 0)
WRAPPER(trap_opaque2, 2, 0, 0)
WRAPPER(trap_transp1, 1, 1, 0)
WRAPPER(trap_transp2, 2, 1, 0)
WRAPPER(trap_opaque1w, 1, 0, 1)
WRAPPER(trap_opaque2w, 2, 0, 1)
WRAPPER(trap_transp1w, 1, 1, 1)
WRAPPER(trap_transp2w, 2, 1, 1)
#undef WRAPPER
