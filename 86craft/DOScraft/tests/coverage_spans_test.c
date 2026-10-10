/* Pure correctness experiment. Synthetic exact painter emission order, not
 * production raster integration or native/486 performance evidence. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "coverage_spans.h"

#define PIXELS (DOS_COVERAGE_MAX_WIDTH * DOS_COVERAGE_MAX_HEIGHT)
#define EVENTS 12000
#define PRIMITIVES 32
struct GuardedCoverage {
    uint32_t prefix[8];
    struct DosCoverageSpans state;
    uint32_t suffix[8];
};
static struct GuardedCoverage store;
static struct DosCoverageSpans before, after_reset;
static unsigned char oracle[PIXELS];
static int32_t width, height;
static uint32_t random_state = UINT32_C(0x486c0ffe);

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void guards(void)
{
    unsigned int i;
    for (i = 0; i < 8; ++i) {
        assert(store.prefix[i] == UINT32_C(0xa5a5a5a5));
        assert(store.suffix[i] == UINT32_C(0xa5a5a5a5));
    }
}

static void check_row(int32_t y)
{
    int32_t x, stride = (width + 31) / 32;
    if (y < 0 || y >= height) return;
    for (x = 0; x < stride * 32; ++x) {
        int bit = (store.state.words[y * stride + x / 32] >> (x % 32)) & 1u;
        int wanted = x < width ? oracle[y * width + x] : 0;
        assert(bit == wanted);
    }
}

static void check_all(void)
{
    int32_t y, active = height * ((width + 31) / 32);
    guards();
    assert(store.state.width == width && store.state.height == height);
    assert(store.state.row_stride == (width + 31) / 32);
    for (y = 0; y < height; ++y) check_row(y);
    assert(memcmp(store.state.words + active, after_reset.words + active,
                  (DOS_COVERAGE_MAX_WORDS - active) * sizeof(uint32_t)) == 0);
}

static void check_outside_row(int32_t y)
{
    int32_t first = y * store.state.row_stride;
    int32_t end = first + store.state.row_stride;
    assert(store.state.width == before.width && store.state.height == before.height &&
           store.state.row_stride == before.row_stride);
    assert(memcmp(store.state.words, before.words, first * sizeof(uint32_t)) == 0);
    assert(memcmp(store.state.words + end, before.words + end,
                  (DOS_COVERAGE_MAX_WORDS - end) * sizeof(uint32_t)) == 0);
}

static void reset(int32_t w, int32_t h)
{
    int32_t active = h * ((w + 31) / 32), i;
    before = store.state;
    assert(dos_coverage_reset(&store.state, w, h) == 1);
    width = w; height = h;
    memset(oracle, 0, sizeof(oracle));
    for (i = 0; i < active; ++i) assert(store.state.words[i] == 0);
    assert(memcmp(store.state.words + active, before.words + active,
                  (DOS_COVERAGE_MAX_WORDS - active) * sizeof(uint32_t)) == 0);
    after_reset = store.state;
    check_all();
}

/* Oracle operates only on individual pixels, never word masks/bit scans. */
static int oracle_span(int32_t y, int32_t left, int32_t right)
{
    int32_t x;
    int any = 0;
    if (y < 0 || y >= height) return -1;
    for (x = 0; x < width; ++x)
        if (x >= left && x < right) {
            oracle[y * width + x] = 1;
            any = 1;
        }
    return any;
}

static void mark_span(int32_t y, int32_t left, int32_t right)
{
    int expected = oracle_span(y, left, right);
    before = store.state;
    assert(dos_coverage_mark_span(&store.state, y, left, right) == expected);
    if (expected <= 0) assert(memcmp(&before, &store.state, sizeof(before)) == 0);
    else check_outside_row(y);
    check_row(y);
    guards();
}

static void mark_pixel(int32_t x, int32_t y)
{
    int expected = -1;
    before = store.state;
    if (x >= 0 && x < width && y >= 0 && y < height) {
        expected = !oracle[y * width + x];
        oracle[y * width + x] = 1;
    }
    assert(dos_coverage_mark_pixel(&store.state, x, y) == expected);
    if (expected <= 0) assert(memcmp(&before, &store.state, sizeof(before)) == 0);
    else check_outside_row(y);
    check_row(y);
    guards();
}

static int oracle_find(int32_t y, int32_t left, int32_t right,
                       int32_t *start, int32_t *end)
{
    int32_t x;
    if (y < 0 || y >= height) return -1;
    for (x = 0; x < width; ++x) {
        if (x < left || x >= right || oracle[y * width + x]) continue;
        *start = x;
        do { ++x; } while (x < width && x < right && !oracle[y * width + x]);
        *end = x;
        return 1;
    }
    return 0;
}

static int find(int32_t y, int32_t left, int32_t right, int32_t *end_out)
{
    int32_t a = -777, b = -888, expected_a = a, expected_b = b;
    int expected = oracle_find(y, left, right, &expected_a, &expected_b);
    before = store.state;
    assert(dos_coverage_find_run(&store.state, y, left, right, &a, &b) == expected);
    assert(a == expected_a && b == expected_b);
    assert(memcmp(&before, &store.state, sizeof(before)) == 0);
    if (expected == 1) assert(a >= 0 && a < b && b <= width);
    if (end_out) *end_out = b;
    guards();
    return expected;
}

static void enumerate(int32_t y)
{
    int32_t left = INT32_MIN, end, count = 0, expected = 0, x;
    for (x = 0; x < width; ++x)
        if (!oracle[y * width + x] && (!x || oracle[y * width + x - 1])) ++expected;
    while (find(y, left, INT32_MAX, &end) == 1) {
        assert(end > left);
        left = end;
        assert(++count <= width);
    }
    assert(count == expected);
}

static void test_boundaries(void)
{
    int32_t w, x, left, right, pattern;
    memset(&store, 0xa5, sizeof(store));
    for (w = 1; w <= DOS_COVERAGE_MAX_WIDTH; ++w) {
        reset(w, 3);
        find(0, INT32_MIN, INT32_MAX, NULL);
        /* Every pixel, including bit 31 and every word/last-row boundary. */
        for (x = 0; x < w; ++x) {
            mark_pixel(x, 2);
            mark_pixel(x, 2);
            find(2, 0, w, NULL);
        }
        for (x = 0; x < w; x += 2) mark_pixel(x, 0);
        enumerate(0);
        enumerate(1);
        enumerate(2);
        mark_span(1, INT32_MIN, INT32_MAX);
        mark_span(1, -1, w + 1);
        find(1, INT32_MIN, INT32_MAX, NULL);
        check_all();
    }
    /* Exhaustive interval intersections for odd/tiny and word-edge widths. */
    for (w = 1; w <= 33; ++w) for (pattern = 0; pattern < 4; ++pattern) {
        reset(w, 1);
        for (x = 0; x < w; ++x)
            if ((pattern == 1 && x % 2) || (pattern == 2 && x % 7 == 0) || pattern == 3)
                mark_pixel(x, 0);
        for (left = -2; left <= w + 2; ++left)
            for (right = -2; right <= w + 2; ++right) find(0, left, right, NULL);
    }
    reset(320, 200);
    /* One uncovered run crosses nine words; padding tests use every odd width. */
    mark_span(199, 0, 5);
    mark_span(199, 307, 320);
    find(199, 0, 320, NULL);
    find(199, 31, 289, NULL);
    mark_span(199, 31, 289);
    enumerate(199);
    check_all();
    puts("PASS: all 320 widths, exhaustive clipped intervals, word boundaries and maximal runs");
}

static void test_invalid(void)
{
    static const int32_t bad[] = {INT32_MIN, -1, 0, 321, INT32_MAX};
    static const int32_t coords[] = {INT32_MIN, -321, -1, 0, 1, 31, 32, 33, 319, 320, INT32_MAX};
    unsigned int i, j;
    int32_t a, b;
    struct DosCoverageSpans invalid;
    memset(&store, 0xa5, sizeof(store));
    reset(33, 2);
    mark_pixel(32, 1);
    before = store.state;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(dos_coverage_reset(&store.state, bad[i], 1) == -1);
        assert(dos_coverage_reset(&store.state, 1, bad[i]) == -1);
        assert(memcmp(&before, &store.state, sizeof(before)) == 0);
    }
    assert(dos_coverage_reset(&store.state, 1, 201) == -1);
    assert(dos_coverage_reset(NULL, 320, 200) == -1);
    a = 17; b = 19;
    assert(dos_coverage_find_run(NULL, 0, 0, 1, &a, &b) == -1);
    assert(dos_coverage_mark_span(NULL, 0, 0, 1) == -1);
    assert(dos_coverage_mark_pixel(NULL, 0, 0) == -1);
    assert(dos_coverage_find_run(&store.state, 0, 0, 33, NULL, &b) == -1);
    assert(dos_coverage_find_run(&store.state, 0, 0, 33, &a, NULL) == -1);
    assert(a == 17 && b == 19);
    assert(memcmp(&before, &store.state, sizeof(before)) == 0);
    for (i = 0; i < sizeof(coords) / sizeof(coords[0]); ++i)
        for (j = 0; j < sizeof(coords) / sizeof(coords[0]); ++j) {
            mark_pixel(coords[i], coords[j]);
            mark_span(coords[j], coords[i], coords[i]);
            find(coords[j], coords[i], coords[i], NULL);
            mark_span(0, coords[i], coords[j]);
            find(0, coords[i], coords[j], NULL);
        }
    /* Zero-initialized and corrupted initialized metadata are safely rejected. */
    for (i = 0; i < 9; ++i) {
        invalid = store.state;
        switch (i) {
        case 0: memset(&invalid, 0, sizeof(invalid)); break;
        case 1: invalid.width = INT32_MIN; break;
        case 2: invalid.width = INT32_MAX; break;
        case 3: invalid.height = -1; break;
        case 4: invalid.height = 201; break;
        case 5: invalid.row_stride = 0; break;
        case 6: invalid.row_stride = INT32_MAX; break;
        case 7: invalid.row_stride = 1; break;
        default: invalid.height = 0; break;
        }
        before = invalid;
        a = 17; b = 19;
        assert(dos_coverage_find_run(&invalid, 0, INT32_MIN, INT32_MAX, &a, &b) == -1);
        assert(dos_coverage_mark_span(&invalid, 0, INT32_MIN, INT32_MAX) == -1);
        assert(dos_coverage_mark_pixel(&invalid, 0, 0) == -1);
        assert(a == 17 && b == 19 && memcmp(&before, &invalid, sizeof(before)) == 0);
    }
    check_all();
    puts("PASS: invalid dimensions/state/pointers/coordinates, extreme clipping and no rejected-call writes");
}

static int32_t random_coord(int32_t limit)
{
    uint32_t n = next_random();
    if ((n & 63u) == 0) return INT32_MIN;
    if ((n & 63u) == 1) return INT32_MAX;
    return (int32_t)(n % (uint32_t)(limit + 70)) - 35;
}

static void trace(unsigned int steps, int mixed)
{
    unsigned int i;
    for (i = 0; i < steps; ++i) {
        int32_t y = random_coord(height), left = random_coord(width), right = random_coord(width);
        uint32_t op = next_random() % 19u;
        if (op == 0) {
            int32_t w = mixed ? 1 + (int32_t)(next_random() % 320u) : width;
            int32_t h = mixed ? 1 + (int32_t)(next_random() % 200u) : height;
            reset(w, h);
        } else if (op < 6) mark_span(y, left, right);
        else if (op < 10) mark_pixel(left, y);
        else if (op == 10) mark_span((int32_t)(next_random() % (uint32_t)height), INT32_MIN, INT32_MAX);
        else find(y, left, right, NULL);
        if (i % 251u == 0) { check_all(); enumerate((int32_t)(next_random() % (uint32_t)height)); }
    }
    check_all();
}

static void test_random(void)
{
    static const int32_t sizes[][2] = {
        {1, 1}, {1, 200}, {7, 17}, {31, 1}, {31, 200}, {32, 200}, {33, 1}, {33, 200},
        {63, 99}, {64, 100}, {65, 101}, {127, 199}, {159, 100}, {160, 100},
        {161, 100}, {287, 200}, {319, 200}, {320, 200}
    };
    unsigned int i;
    memset(&store, 0xa5, sizeof(store));
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        reset(sizes[i][0], sizes[i][1]);
        trace(6000, 0);
    }
    trace(20000, 1);
    puts("PASS: 128000 random reset/mark/find interleavings, independent per-pixel oracle and guards");
}

struct Event { int32_t x, y; unsigned char color; };
struct Primitive { int32_t start, count, tie; };
static struct Event events[EVENTS];
static struct Primitive primitives[PRIMITIVES];
static unsigned char painter[PIXELS], reverse[PIXELS], wrong[PIXELS];
static unsigned char claimed[PIXELS];
static int32_t event_count, primitive_count;

static void begin_primitive(int32_t tie)
{
    assert(primitive_count < PRIMITIVES);
    primitives[primitive_count].start = event_count;
    primitives[primitive_count].count = 0;
    primitives[primitive_count++].tie = tie;
}

static void emit(int32_t x, int32_t y, unsigned char color)
{
    assert(event_count < EVENTS && primitive_count > 0);
    events[event_count].x = x; events[event_count].y = y; events[event_count].color = color;
    ++event_count;
    ++primitives[primitive_count - 1].count;
}

static int inside(const struct Event *event)
{
    return event->x >= 0 && event->x < width && event->y >= 0 && event->y < height;
}

/* Actual color write is opaque/nonzero; zero never claims coverage. */
static void front_event(const struct Event *event, unsigned char *target)
{
    if (inside(event) && event->color &&
        dos_coverage_mark_pixel(&store.state, event->x, event->y) == 1)
        target[event->y * width + event->x] = event->color;
}

static void compare_painters(void)
{
    int32_t p, i, position;
    memset(painter, 197, sizeof(painter));
    memset(reverse, 197, sizeof(reverse));
    memset(claimed, 0, sizeof(claimed));
    for (p = 0; p < primitive_count; ++p) {
        const struct Primitive *primitive = &primitives[p];
        for (i = 0; i < primitive->count; ++i) {
            const struct Event *event = &events[primitive->start + i];
            if (inside(event) && event->color) {
                position = event->y * width + event->x;
                painter[position] = event->color;
                claimed[position] = 1;
            }
        }
    }
    reset(width, height);
    for (p = primitive_count; p-- > 0;) {
        const struct Primitive *primitive = &primitives[p];
        /* Reverse within primitives too: the last painter write must win. */
        for (i = primitive->count; i-- > 0;)
            front_event(&events[primitive->start + i], reverse);
    }
    assert(memcmp(painter, reverse, sizeof(painter)) == 0);
    memcpy(oracle, claimed, sizeof(oracle));
    check_all();
}

static void test_painter(void)
{
    int32_t scene, p, i, scale, x;
    memset(&store, 0xa5, sizeof(store));
    reset(33, 3);
    event_count = primitive_count = 0;
    begin_primitive(10);
    emit(0, 0, 3); emit(1, 0, 4); emit(31, 1, 5); emit(32, 1, 6);
    begin_primitive(10); /* Exact tie: later painter primitive wins. */
    emit(0, 0, 7); emit(0, 0, 9); /* Repeated writes within the same primitive. */
    emit(1, 0, 0); emit(2, 0, 0); /* Lower color shows through; unclaimed hole. */
    emit(31, 1, 0); emit(32, 1, 11);
    compare_painters();
    assert(painter[0] == 9 && painter[1] == 4 && painter[2] == 197);
    assert(painter[33 + 31] == 5 && painter[33 + 32] == 11);
    assert(dos_coverage_mark_pixel(&store.state, 2, 0) == 1); /* Zero left a hole. */
    /* Palette zero can also be opaque (flat/material semantics). The kernel
     * claims it normally when the caller deliberately performs that write. */
    reset(33, 3);
    assert(dos_coverage_mark_pixel(&store.state, 0, 0) == 1);
    reverse[0] = 0;
    assert(dos_coverage_mark_pixel(&store.state, 0, 0) == 0 && reverse[0] == 0);
    /* Reset before the independent transparent-event negative controls. */
    /* Negative controls make the exact reversal/zero contract observable. */
    memset(wrong, 197, sizeof(wrong));
    reset(width, height);
    for (p = primitive_count; p-- > 0;)
        for (i = 0; i < primitives[p].count; ++i)
            front_event(&events[primitives[p].start + i], wrong);
    assert(wrong[0] == 7 && wrong[0] != painter[0]);
    memset(wrong, 197, sizeof(wrong));
    reset(width, height);
    for (i = event_count; i-- > 0;) {
        const struct Event *event = &events[i];
        if (inside(event) && dos_coverage_mark_pixel(&store.state, event->x, event->y) == 1 && event->color)
            wrong[event->y * width + event->x] = event->color;
    }
    assert(wrong[1] == 197 && wrong[1] != painter[1]);
    for (scale = 1; scale <= 2; ++scale) for (scene = 0; scene < 128; ++scene) {
        reset(320 / scale, 200 / scale);
        event_count = primitive_count = 0;
        for (p = 0; p < PRIMITIVES; ++p) {
            /* Ties deliberately frequent. Preserve supplied order, no re-sort. */
            begin_primitive(p / 3);
            for (i = 0; i < 250; ++i) {
                unsigned char color = i % 4 == 0 ? 0 : (unsigned char)(1u + next_random() % 255u);
                int32_t px = i % 5 == 0 ? 31 : random_coord(width);
                int32_t py = i % 5 == 0 ? 1 : random_coord(height);
                emit(px, py, color);
                if (i % 17 == 0) { emit(px, py, (unsigned char)(p + 1)); emit(px, py, 0); }
            }
        }
        assert(primitives[0].tie == primitives[1].tie);
        compare_painters();
    }
    /* Span consumer: enumerate holes, write each opaque run, then mark it.
     * A cutout only marks its nonzero samples, so lower spans fill its holes. */
    reset(65, 1);
    memset(reverse, 197, sizeof(reverse));
    for (x = 0; x < width; ++x) {
        struct Event cutout = {x, 0, x % 3 ? 21 : 0};
        front_event(&cutout, reverse);
    }
    x = 0;
    for (;;) {
        int32_t left, right;
        int result = dos_coverage_find_run(&store.state, 0, x, width, &left, &right);
        assert(result >= 0);
        if (!result) break;
        for (x = left; x < right; ++x) reverse[x] = 13;
        assert(dos_coverage_mark_span(&store.state, 0, left, right) == 1);
        x = right;
    }
    for (x = 0; x < width; ++x) { assert(reverse[x] == (x % 3 ? 21 : 13)); oracle[x] = 1; }
    check_all();
    puts("PASS: 256 exact painter sequence reversals at 160x100/320x200; ties, holes, repeated overlap, zero negative controls and opaque runs");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "boundaries")) test_boundaries();
    else if (!strcmp(argv[1], "invalid")) test_invalid();
    else if (!strcmp(argv[1], "random")) test_random();
    else if (!strcmp(argv[1], "painter")) test_painter();
    else return 2;
    return 0;
}
