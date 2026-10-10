#include <limits.h>
#include "coverage_spans.h"

static int valid_state(const struct DosCoverageSpans *state)
{
    return state && state->width >= 1 && state->width <= DOS_COVERAGE_MAX_WIDTH &&
        state->height >= 1 && state->height <= DOS_COVERAGE_MAX_HEIGHT &&
        state->row_stride == (state->width + 31) / 32;
}

/* Only called after proving bits != 0; BSF on i486 has no zero result. */
static int32_t first_bit(uint32_t bits)
{
#if (defined(__GNUC__) || defined(__clang__)) && UINT_MAX == 0xffffffffu && \
    !defined(DOS_COVERAGE_PORTABLE)
    return (int32_t)__builtin_ctz((unsigned int)bits);
#else
    int32_t bit = 0;
    while (!(bits & UINT32_C(1))) { bits >>= 1; ++bit; }
    return bit;
#endif
}

/* 0 <= left < right <= 32: neither shift can ever be 32. */
static uint32_t interval_mask(int32_t left, int32_t right)
{
    return (UINT32_MAX << left) & (UINT32_MAX >> (32 - right));
}

/* Seek one bit of the requested kind, or return right. Bounds already clipped. */
static int32_t seek(const uint32_t *row, int32_t left, int32_t right, int covered)
{
    while (left < right) {
        int32_t word = left >> 5;
        int32_t base = word << 5;
        int32_t end = right - base;
        uint32_t bits;
        if (end > 32) end = 32;
        bits = covered ? row[word] : ~row[word];
        bits &= interval_mask(left - base, end);
        if (bits) return base + first_bit(bits);
        left = base + end;
    }
    return right;
}

int dos_coverage_reset(struct DosCoverageSpans *state, int32_t width, int32_t height)
{
    int32_t i, stride;
    if (!state || width < 1 || width > DOS_COVERAGE_MAX_WIDTH ||
        height < 1 || height > DOS_COVERAGE_MAX_HEIGHT) return -1;
    stride = (width + 31) / 32;
    state->width = width;
    state->height = height;
    state->row_stride = stride;
    for (i = 0; i < height * stride; ++i) state->words[i] = 0;
    return 1;
}

int dos_coverage_find_run(const struct DosCoverageSpans *state, int32_t y,
                          int32_t left, int32_t right,
                          int32_t *run_left, int32_t *run_right)
{
    const uint32_t *row;
    int32_t start;
    if (!valid_state(state) || y < 0 || y >= state->height ||
        !run_left || !run_right) return -1;
    if (left < 0) left = 0;
    if (right > state->width) right = state->width;
    if (left >= right) return 0;
    row = state->words + y * state->row_stride;
    start = seek(row, left, right, 0);
    if (start == right) return 0;
    *run_left = start;
    *run_right = seek(row, start, right, 1);
    return 1;
}

int dos_coverage_mark_span(struct DosCoverageSpans *state, int32_t y,
                           int32_t left, int32_t right)
{
    uint32_t *row;
    if (!valid_state(state) || y < 0 || y >= state->height) return -1;
    if (left < 0) left = 0;
    if (right > state->width) right = state->width;
    if (left >= right) return 0;
    row = state->words + y * state->row_stride;
    while (left < right) {
        int32_t word = left >> 5;
        int32_t base = word << 5;
        int32_t end = right - base;
        if (end > 32) end = 32;
        row[word] |= interval_mask(left - base, end);
        left = base + end;
    }
    return 1;
}

int dos_coverage_mark_pixel(struct DosCoverageSpans *state, int32_t x, int32_t y)
{
    uint32_t *word, bit;
    if (!valid_state(state) || x < 0 || x >= state->width ||
        y < 0 || y >= state->height) return -1;
    word = state->words + y * state->row_stride + (x >> 5);
    bit = UINT32_C(1) << (x & 31);
    if (*word & bit) return 0;
    *word |= bit;
    return 1;
}
