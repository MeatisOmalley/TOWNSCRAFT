/* Optimized native toolchains may define NDEBUG; checks must still execute. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vga_pack.h"

/* Keep the experimental declaration local: no production header changes. */
int dos_vga_pack_all(const unsigned char *, unsigned int,
                     unsigned char *, unsigned int);

#define GUARD 32u
#define ALIGN_SLACK 16u
#define MAX_PITCH 1025u
#define FRAME_BYTES (4u * DOS_VGA_PLANE_BYTES)
static unsigned char source_storage[GUARD + ALIGN_SLACK + MAX_PITCH * DOS_VGA_HEIGHT + GUARD];
static unsigned char source_before[sizeof(source_storage)];
static unsigned char output_storage[GUARD + ALIGN_SLACK + FRAME_BYTES + GUARD];
static unsigned char expected[FRAME_BYTES];
static uint32_t random_state = UINT32_C(0x8e7a2c91);

static unsigned char random_byte(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (unsigned char)(random_state >> 24);
}

/* Independent reference: reconstruct each plane from linear pixel indices,
 * without calling the existing packer or sharing its nested y/x traversal.
 */
static void reference(const unsigned char *source, unsigned int pitch)
{
    unsigned int plane, index;
    for (plane = 0; plane < 4; ++plane)
        for (index = 0; index < DOS_VGA_PLANE_BYTES; ++index) {
            unsigned int pixel = index * 4u + plane;
            unsigned int row = pixel / DOS_VGA_WIDTH;
            unsigned int column = pixel % DOS_VGA_WIDTH;
            expected[plane * DOS_VGA_PLANE_BYTES + index] = source[row * pitch + column];
        }
}

static void assert_guards(unsigned int start)
{
    unsigned int i;
    for (i = 0; i < start; ++i) assert(output_storage[i] == 0xcd);
    for (i = start + FRAME_BYTES; i < sizeof(output_storage); ++i)
        assert(output_storage[i] == 0xcd);
}

static void test_valid(void)
{
    static const unsigned int pitches[] = {320, 321, 323, 511, 512, 513, MAX_PITCH};
    unsigned int p, offset, pattern, cases = 0;
    for (p = 0; p < sizeof(pitches) / sizeof(pitches[0]); ++p)
        for (offset = 0; offset < ALIGN_SLACK; ++offset)
            for (pattern = 0; pattern < 4; ++pattern) {
                unsigned int x, y, i, pitch = pitches[p];
                unsigned int destination_start = GUARD + ((offset * 7u + 3u) & 15u);
                unsigned char *source = source_storage + GUARD + offset;
                unsigned char *output = output_storage + destination_start;
                unsigned char seen[256] = {0};
                unsigned int capacity = cases & 1u ? FRAME_BYTES + GUARD : FRAME_BYTES;
                memset(source_storage, 0xa5, sizeof(source_storage));
                memset(output_storage, 0xcd, sizeof(output_storage));
                for (y = 0; y < DOS_VGA_HEIGHT; ++y)
                    for (x = 0; x < DOS_VGA_WIDTH; ++x) {
                        unsigned char value = pattern == 0 ? 0 : pattern == 1 ? 255 :
                            pattern == 2 ? (unsigned char)(y * DOS_VGA_WIDTH + x) : random_byte();
                        source[y * pitch + x] = value;
                        seen[value] = 1;
                    }
                if (pattern >= 2)
                    for (i = 0; i < 256; ++i) assert(seen[i]);
                memcpy(source_before, source_storage, sizeof(source_storage));
                reference(source, pitch);
                assert(dos_vga_pack_all(source, pitch, output, capacity) == 0);
                assert(memcmp(output, expected, FRAME_BYTES) == 0);
                assert(memcmp(source_storage, source_before, sizeof(source_storage)) == 0);
                assert_guards(destination_start);
                ++cases;
            }
    assert(cases == 448);
    puts("PASS: 448 frames, independent four-plane reference, all byte values, pitches, unaligned buffers and guards");
}

static void assert_rejected(const unsigned char *source, unsigned int pitch,
                            unsigned char *output, unsigned int capacity)
{
    unsigned int i;
    memset(output_storage, 0xcd, sizeof(output_storage));
    memcpy(source_before, source_storage, sizeof(source_storage));
    assert(dos_vga_pack_all(source, pitch, output, capacity) == -1);
    for (i = 0; i < sizeof(output_storage); ++i) assert(output_storage[i] == 0xcd);
    assert(memcmp(source_storage, source_before, sizeof(source_storage)) == 0);
}

static void test_invalid(void)
{
    static const unsigned int short_pitches[] = {0, 1, DOS_VGA_WIDTH - 1};
    static const unsigned int short_capacities[] = {0, 1, DOS_VGA_PLANE_BYTES, FRAME_BYTES - 1};
    const unsigned char *source = source_storage + GUARD + 1;
    unsigned char *output = output_storage + GUARD + 3;
    unsigned int i;
    memset(source_storage, 0x7b, sizeof(source_storage));
    for (i = 0; i < sizeof(short_pitches) / sizeof(short_pitches[0]); ++i)
        assert_rejected(source, short_pitches[i], output, FRAME_BYTES);
    for (i = 0; i < sizeof(short_capacities) / sizeof(short_capacities[0]); ++i)
        assert_rejected(source, DOS_VGA_WIDTH, output, short_capacities[i]);
    assert_rejected(NULL, DOS_VGA_WIDTH, output, FRAME_BYTES);
    assert_rejected(source, DOS_VGA_WIDTH, NULL, FRAME_BYTES);
    assert_rejected(NULL, UINT_MAX, output, UINT_MAX);
    assert_rejected(source, UINT_MAX, output, FRAME_BYTES - 1);
    assert_rejected(NULL, 0, NULL, 0);
    puts("PASS: invalid null, pitch and capacity rejected before any write");
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        test_valid();
        test_invalid();
    } else if (argc == 2 && strcmp(argv[1], "valid") == 0) test_valid();
    else if (argc == 2 && strcmp(argv[1], "invalid") == 0) test_invalid();
    else return 2;
    return 0;
}
