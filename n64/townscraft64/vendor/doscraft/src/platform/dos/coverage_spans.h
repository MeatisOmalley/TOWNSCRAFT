#ifndef DOSCRAFT_COVERAGE_SPANS_H
#define DOSCRAFT_COVERAGE_SPANS_H

#include <stdint.h>

#define DOS_COVERAGE_MAX_WIDTH 320
#define DOS_COVERAGE_MAX_HEIGHT 200
#define DOS_COVERAGE_MAX_WORDS (DOS_COVERAGE_MAX_HEIGHT * 10)

/* Experimental exact coverage only; no depth, texture, allocation or I/O.
 * Use static or caller-owned storage (8000 bytes of bits plus metadata).
 * Initialize with reset before use; fields are private to these functions.
 * One set bit means an actual opaque write has claimed that pixel.
 * Active words are packed by row, stride = (width + 31) / 32.
 * Calls are single-threaded. All pointer arguments must refer to valid,
 * aligned storage; find outputs must be distinct and disjoint from state.
 */
struct DosCoverageSpans {
    int32_t width, height, row_stride;
    uint32_t words[DOS_COVERAGE_MAX_WORDS];
};

/* Return 1 and clear active words for 1 <= width <= 320, 1 <= height <= 200.
 * Return -1 for NULL or invalid dimensions, preserving the entire old state.
 * Inactive storage is untouched and never accessed by row operations.
 */
int dos_coverage_reset(struct DosCoverageSpans *state, int32_t width, int32_t height);

/* Find the first maximal uncovered run inside [left,right) on row y.
 * Intersect horizontal bounds with [0,width); never swap reversed bounds.
 * Maximality is relative to this clipped interval, including across words.
 * Return 1 and store [run_left,run_right), 0 if empty or fully covered,
 * or -1 for invalid state, y outside [0,height), or NULL output pointers.
 * Outputs are untouched on 0/-1; state is always untouched.
 * Resume at run_right to enumerate the next run.
 */
int dos_coverage_find_run(const struct DosCoverageSpans *state, int32_t y,
                          int32_t left, int32_t right,
                          int32_t *run_left, int32_t *run_right);

/* Claim a span known by the caller to have actually written opaque pixels.
 * Same clipping/row rules as find. Return 1 for a nonempty clipped span
 * (even if already covered), 0 for empty, -1 for invalid state or row.
 * Empty/rejected calls do not write. Never use for a cutout's bounding span.
 */
int dos_coverage_mark_span(struct DosCoverageSpans *state, int32_t y,
                           int32_t left, int32_t right);

/* Test and claim one opaque pixel: 1 on its first claim, 0 if already claimed,
 * -1 for invalid state or coordinates; no coordinate clipping.
 * For cutouts, test the sampled texel FIRST: zero must neither call this
 * function nor write color. On 1, perform the corresponding opaque write.
 * This kernel deliberately has no knowledge of texels or color zero.
 */
int dos_coverage_mark_pixel(struct DosCoverageSpans *state, int32_t x, int32_t y);

#endif
