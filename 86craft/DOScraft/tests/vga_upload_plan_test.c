#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vga_upload_plan.h"

#define GUARD 32u
#define OFFSETS 16u
#define MAX_STRIDE (DOS_VGA_UPLOAD_MAX_ROWS * DOS_VGA_UPLOAD_ROW_BYTES + 803u)
#define SOURCE_BYTES (GUARD + OFFSETS + 4u * MAX_STRIDE + GUARD)
static unsigned char source_store[SOURCE_BYTES], source_before[SOURCE_BYTES];
static unsigned char reconstructed[4][DOS_VGA_UPLOAD_MAX_ROWS][DOS_VGA_UPLOAD_ROW_BYTES];
static unsigned char expected_mask[4][DOS_VGA_UPLOAD_MAX_ROWS];
static unsigned char written[4][DOS_VGA_UPLOAD_MAX_ROWS];
struct UploadOutput {
    unsigned char prefix[GUARD];
    struct DosVgaRun runs[DOS_VGA_UPLOAD_MAX_RUNS + 4u];
    unsigned char suffix[GUARD];
};
static struct UploadOutput output;
static unsigned char output_before[sizeof(output)];
static uint32_t random_state = UINT32_C(0x8048625a);

static unsigned int random_value(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

/* Independent byte-only identity oracle, independent of host word order. */
static int same_bytes(const unsigned char *a, const unsigned char *b)
{
    unsigned int x;
    int equal = 1;
    for (x = 0; x < DOS_VGA_UPLOAD_ROW_BYTES; ++x)
        if (a[x] != b[x]) equal = 0;
    return equal;
}

static unsigned int oracle(const unsigned char *source, unsigned int stride,
                            unsigned int rows, unsigned int *run_count)
{
    unsigned int plane, row, bytes = 0;
    memset(expected_mask, 0, sizeof(expected_mask));
    *run_count = 0;
    for (row = 0; row < rows; ++row) {
        const unsigned char *p[4];
        unsigned int pair01, pair23, all;
        for (plane = 0; plane < 4; ++plane)
            p[plane] = source + plane * stride + row * DOS_VGA_UPLOAD_ROW_BYTES;
        pair01 = (unsigned int)same_bytes(p[0], p[1]);
        pair23 = (unsigned int)same_bytes(p[2], p[3]);
        all = pair01 && pair23 && same_bytes(p[0], p[2]);
        /* Declare allowed membership per source owner using the byte oracle. */
        for (plane = 0; plane < 4; ++plane) {
            unsigned int mask;
            if (all) mask = plane == 0 ? 15u : 0u;
            else if (plane < 2 && pair01) mask = plane == 0 ? 3u : 0u;
            else if (plane >= 2 && pair23) mask = plane == 2 ? 12u : 0u;
            else mask = 1u << plane;
            expected_mask[plane][row] = (unsigned char)mask;
            if (mask) bytes += DOS_VGA_UPLOAD_ROW_BYTES;
        }
    }
    for (plane = 0; plane < 4; ++plane)
        for (row = 0; row < rows; ++row)
            if (expected_mask[plane][row] &&
                (!row || expected_mask[plane][row - 1] != expected_mask[plane][row]))
                ++*run_count;
    return bytes;
}

static unsigned int check_plan(unsigned int offset, unsigned int stride, unsigned int rows)
{
    const unsigned char *source = source_store + GUARD + offset;
    unsigned int i, plane, row, x, bytes = 0, expected_runs;
    unsigned int expected_bytes = oracle(source, stride, rows, &expected_runs);
    unsigned int capacity = 4u * rows;
    int count;
    memset(&output, 0xcd, sizeof(output));
    memcpy(output_before, &output, sizeof(output));
    memcpy(source_before, source_store, sizeof(source_store));
    memset(reconstructed, 0xa5, sizeof(reconstructed));
    memset(written, 0, sizeof(written));
    count = dos_vga_upload_plan(source, stride, rows, output.runs, capacity);
    assert(count >= 0 && (unsigned int)count <= capacity);
    assert((unsigned int)count == expected_runs);
    for (i = 0; i < (unsigned int)count; ++i) {
        const struct DosVgaRun *run = &output.runs[i];
        assert(run->plane < 4 && run->mask && run->mask < 16);
        assert(run->mask & (1u << run->plane));
        assert(!(run->mask & ((1u << run->plane) - 1u)));
        assert(run->rows && run->row < rows && run->rows <= rows - run->row);
        if (i) {
            const struct DosVgaRun *previous = &output.runs[i - 1];
            assert(previous->plane <= run->plane);
            if (previous->plane == run->plane) {
                assert(previous->row + previous->rows <= run->row);
                assert(previous->mask != run->mask || previous->row + previous->rows != run->row);
            }
        }
        bytes += run->rows * DOS_VGA_UPLOAD_ROW_BYTES;
        /* Software VGA: one source write broadcasts to every selected plane. */
        for (row = run->row; row < (unsigned int)run->row + run->rows; ++row) {
            assert(expected_mask[run->plane][row] == run->mask);
            for (plane = 0; plane < 4; ++plane) {
                if (!(run->mask & (1u << plane))) continue;
                assert(!written[plane][row]);
                written[plane][row] = 1;
                for (x = 0; x < DOS_VGA_UPLOAD_ROW_BYTES; ++x)
                    reconstructed[plane][row][x] =
                        source[run->plane * stride + row * DOS_VGA_UPLOAD_ROW_BYTES + x];
            }
        }
    }
    assert(bytes == expected_bytes);
    for (plane = 0; plane < 4; ++plane)
        for (row = 0; row < DOS_VGA_UPLOAD_MAX_ROWS; ++row) {
            assert(written[plane][row] == (row < rows));
            for (x = 0; x < DOS_VGA_UPLOAD_ROW_BYTES; ++x) {
                unsigned char wanted = row < rows ?
                    source[plane * stride + row * DOS_VGA_UPLOAD_ROW_BYTES + x] : 0xa5;
                assert(reconstructed[plane][row][x] == wanted);
            }
        }
    /* Check prefix, unused entries (including capacity slack), and suffix. */
    for (i = 0; i < offsetof(struct UploadOutput, runs); ++i)
        assert(((const unsigned char *)&output)[i] == output_before[i]);
    for (i = (unsigned int)(offsetof(struct UploadOutput, runs) + count * sizeof(output.runs[0]));
         i < sizeof(output); ++i)
        assert(((const unsigned char *)&output)[i] == output_before[i]);
    assert(memcmp(source_store, source_before, sizeof(source_store)) == 0);
    return bytes;
}

static void prepare(unsigned int offset, unsigned int stride, unsigned int rows, unsigned int pattern)
{
    unsigned char *source = source_store + GUARD + offset;
    unsigned int row, x;
    memset(source_store, 0x7b, sizeof(source_store));
    for (row = 0; row < rows; ++row) {
        unsigned int mode = pattern == 5 ? row % 5u : pattern;
        for (x = 0; x < DOS_VGA_UPLOAD_ROW_BYTES; ++x) {
            unsigned char a = (unsigned char)random_value(), b = a ^ 0x55u;
            unsigned char values[4];
            if (mode == 0) values[0] = values[1] = values[2] = values[3] = a;
            else if (mode == 1) { values[0] = values[1] = a; values[2] = values[3] = b; }
            else if (mode == 2) { values[0] = values[1] = a; values[2] = b; values[3] = a ^ 0xaau; }
            else if (mode == 3) { values[0] = a; values[1] = b; values[2] = values[3] = a ^ 0xaau; }
            else if (mode == 6) {
                unsigned int plane;
                for (plane = 0; plane < 4; ++plane) values[plane] = (unsigned char)random_value();
            } else if (mode == 7) {
                values[0] = values[1] = values[2] = values[3] = a;
                if (x == row % DOS_VGA_UPLOAD_ROW_BYTES) values[row % 4u] ^= 1u;
            } else if (mode == 8) { values[0] = values[2] = a; values[1] = values[3] = b; }
            else { values[0] = a; values[1] = a ^ 0x11u; values[2] = a ^ 0x22u; values[3] = a ^ 0x44u; }
            source[row * DOS_VGA_UPLOAD_ROW_BYTES + x] = values[0];
            source[stride + row * DOS_VGA_UPLOAD_ROW_BYTES + x] = values[1];
            source[2u * stride + row * DOS_VGA_UPLOAD_ROW_BYTES + x] = values[2];
            source[3u * stride + row * DOS_VGA_UPLOAD_ROW_BYTES + x] = values[3];
        }
    }
}

static void test_matrix(void)
{
    static const unsigned int heights[] = {0, 1, 2, 3, 7, 40, 100, 200, 239, 240};
    static const unsigned int padding[] = {0, 1, 3, 17, 127, 803};
    unsigned int h, p, offset, pattern, cases = 0;
    for (h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h)
        for (p = 0; p < sizeof(padding) / sizeof(padding[0]); ++p)
            for (offset = 0; offset < OFFSETS; ++offset)
                for (pattern = 0; pattern < 9; ++pattern) {
                    unsigned int rows = heights[h], stride = rows * DOS_VGA_UPLOAD_ROW_BYTES + padding[p];
                    unsigned int bytes, writes = pattern == 0 ? 1u : pattern == 1 ? 2u :
                        pattern == 2 || pattern == 3 || pattern == 7 ? 3u : 4u;
                    prepare(offset, stride, rows, pattern);
                    bytes = check_plan(offset, stride, rows);
                    if (pattern != 5 && pattern != 6) assert(bytes == rows * DOS_VGA_UPLOAD_ROW_BYTES * writes);
                    ++cases;
                }
    assert(cases == 8640);
    for (h = 0; h < 128; ++h) {
        unsigned int rows = random_value() % 241u;
        unsigned int stride = rows * DOS_VGA_UPLOAD_ROW_BYTES + random_value() % 804u;
        offset = random_value() % OFFSETS;
        prepare(offset, stride, rows, 6);
        check_plan(offset, stride, rows);
    }
    puts("PASS: 8640 matrix cases and 128 random strides/heights, software VGA identity, coalescing and byte counts");
}

static void test_single_bits(void)
{
    unsigned int plane, x, bit, cases = 0;
    for (plane = 0; plane < 4; ++plane)
        for (x = 0; x < DOS_VGA_UPLOAD_ROW_BYTES; ++x)
            for (bit = 0; bit < 8; ++bit) {
                prepare(1, 243, 3, 0);
                source_store[GUARD + 1u + plane * 243u + DOS_VGA_UPLOAD_ROW_BYTES + x] ^=
                    (unsigned char)(1u << bit);
                assert(check_plan(1, 243, 3) == 400u);
                ++cases;
            }
    assert(cases == 2560);
    puts("PASS: 2560 single-bit differences at every byte/plane preserve exact overlay pixels");
}

static void rejected(const unsigned char *source, unsigned int stride, unsigned int rows,
                      struct DosVgaRun *runs, unsigned int capacity)
{
    memset(&output, 0xcd, sizeof(output));
    memcpy(output_before, &output, sizeof(output));
    memcpy(source_before, source_store, sizeof(source_store));
    assert(dos_vga_upload_plan(source, stride, rows, runs, capacity) == -1);
    assert(memcmp(&output, output_before, sizeof(output)) == 0);
    assert(memcmp(source_store, source_before, sizeof(source_store)) == 0);
}

static void test_invalid(void)
{
    const unsigned char *source = source_store + GUARD + 1;
    unsigned int rows;
    prepare(1, 19200, 240, 0);
    rejected(NULL, 19200, 240, output.runs, 960);
    rejected(source, 19200, 240, NULL, 960);
    rejected(NULL, 0, 0, output.runs, 0);
    rejected(source, 0, 0, NULL, 0);
    rejected(source, UINT_MAX, 241, output.runs, UINT_MAX);
    rejected(source, UINT_MAX, UINT_MAX, output.runs, UINT_MAX);
    for (rows = 1; rows <= DOS_VGA_UPLOAD_MAX_ROWS; ++rows) {
        rejected(source, rows * DOS_VGA_UPLOAD_ROW_BYTES - 1, rows, output.runs, 4u * rows);
        rejected(source, 19200, rows, output.runs, 4u * rows - 1);
    }
    rejected(source, 19200, 240, output.runs, 1); /* One actual run still needs capacity 960. */
    assert(check_plan(1, 0, 0) == 0);
    assert(check_plan(1, UINT_MAX, 0) == 0);
    puts("PASS: NULL, rows, stride, upfront worst-case capacity and zero rows; no rejected-call writes");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (strcmp(argv[1], "matrix") == 0) test_matrix();
    else if (strcmp(argv[1], "bits") == 0) test_single_bits();
    else if (strcmp(argv[1], "invalid") == 0) test_invalid();
    else return 2;
    return 0;
}
