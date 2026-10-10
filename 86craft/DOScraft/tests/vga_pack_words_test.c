/* Independent native experiment; never opens VGA, a UI or an emulator. */
#define _GNU_SOURCE 1
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "vga_pack_words.h"

#define GUARD 32u
#define ALIGNMENTS 16u
#define MAX_PITCH 4097u
#define FRAME_BYTES (4u * DOS_VGA_PLANE_BYTES)
static unsigned char source_storage[GUARD + ALIGNMENTS + MAX_PITCH * DOS_VGA_HEIGHT + GUARD];
static unsigned char source_before[sizeof(source_storage)];
static unsigned char output_storage[GUARD + ALIGNMENTS + FRAME_BYTES + GUARD];
static unsigned char expected[FRAME_BYTES];
static unsigned char original[FRAME_BYTES];
static unsigned char planes[FRAME_BYTES];
static uint32_t random_state = UINT32_C(0x8e7a2c91);

static unsigned char random_byte(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (unsigned char)(random_state >> 24);
}

/* Independent linear-pixel reference, deliberately unlike the block kernel. */
static void reference(const unsigned char *source, unsigned int pitch)
{
    unsigned int pixel;
    for (pixel = 0; pixel < FRAME_BYTES; ++pixel) {
        unsigned int y = pixel / DOS_VGA_WIDTH;
        unsigned int x = pixel % DOS_VGA_WIDTH;
        expected[(x % 4u) * DOS_VGA_PLANE_BYTES +
                 y * (DOS_VGA_WIDTH / 4u) + x / 4u] = source[(size_t)y * pitch + x];
    }
}

static void compare_all(const unsigned char *source, unsigned int pitch,
                        unsigned char *output, unsigned int capacity)
{
    unsigned int plane;
    reference(source, pitch);
    assert(dos_vga_pack_words(source, pitch, output, capacity) == 0);
    assert(dos_vga_pack_all(source, pitch, original, FRAME_BYTES) == 0);
    for (plane = 0; plane < 4; ++plane)
        assert(dos_vga_pack_plane(source, pitch, planes + plane * DOS_VGA_PLANE_BYTES,
                                  DOS_VGA_PLANE_BYTES, plane) == 0);
    assert(memcmp(output, expected, FRAME_BYTES) == 0);
    assert(memcmp(output, original, FRAME_BYTES) == 0);
    assert(memcmp(output, planes, FRAME_BYTES) == 0);
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
    /* Every pitch residue mod 16, tight rows, the measured 512-byte pitch,
     * odd row starts, cache/page boundary strides, all 256 source/dest offset
     * pairs for EACH pitch with arbitrary random pixels. Also test zero, ff
     * and sequential all-byte patterns once per pitch with unaligned buffers.
     */
    static const unsigned int pitches[] = {
        320, 321, 322, 323, 324, 325, 326, 327,
        328, 329, 330, 331, 332, 333, 334, 335,
        511, 512, 513, 1023, 1024, 1025, 4095, 4096, 4097
    };
    unsigned int p, so, dest, cases = 0;
    for (p = 0; p < sizeof(pitches) / sizeof(pitches[0]); ++p)
        for (so = 0; so < ALIGNMENTS; ++so)
            for (dest = 0; dest < ALIGNMENTS; ++dest) {
                unsigned int x, y, i, pitch = pitches[p];
                unsigned int start = GUARD + dest;
                unsigned char *source = source_storage + GUARD + so;
                unsigned char *output = output_storage + start;
                unsigned char seen[256] = {0};
                unsigned int capacity = cases % 3u == 0 ? FRAME_BYTES :
                    cases % 3u == 1 ? FRAME_BYTES + GUARD : UINT_MAX;
                memset(source_storage, 0xa5, sizeof(source_storage));
                memset(output_storage, 0xcd, sizeof(output_storage));
                for (y = 0; y < DOS_VGA_HEIGHT; ++y)
                    for (x = 0; x < DOS_VGA_WIDTH; ++x) {
                        unsigned char value = random_byte();
                        source[(size_t)y * pitch + x] = value;
                        seen[value] = 1;
                    }
                for (i = 0; i < 256; ++i) assert(seen[i]);
                memcpy(source_before, source_storage, sizeof(source_storage));
                compare_all(source, pitch, output, capacity);
                assert(memcmp(source_storage, source_before, sizeof(source_storage)) == 0);
                assert_guards(start);
                ++cases;
            }
    assert(cases == 6400);
    for (p = 0; p < sizeof(pitches) / sizeof(pitches[0]); ++p) {
        unsigned int pattern;
        for (pattern = 0; pattern < 3; ++pattern) {
            unsigned int x, y, pitch = pitches[p];
            unsigned char *source = source_storage + GUARD + 1;
            unsigned char *output = output_storage + GUARD + 3;
            memset(source_storage, 0xa5, sizeof(source_storage));
            memset(output_storage, 0xcd, sizeof(output_storage));
            for (y = 0; y < DOS_VGA_HEIGHT; ++y)
                for (x = 0; x < DOS_VGA_WIDTH; ++x)
                    source[(size_t)y * pitch + x] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                        (unsigned char)(y * DOS_VGA_WIDTH + x);
            memcpy(source_before, source_storage, sizeof(source_storage));
            compare_all(source, pitch, output, FRAME_BYTES);
            assert(memcmp(source_storage, source_before, sizeof(source_storage)) == 0);
            assert_guards(GUARD + 3);
            ++cases;
        }
    }
    assert(cases == 6475);
    puts("PASS: 6475 frames; independent reference, original all-plane and plane extraction parity; 25 pitches, 256 random alignment pairs, all bytes, guards");
}

static void assert_rejected(const unsigned char *source, unsigned int pitch,
                            unsigned char *output, unsigned int capacity)
{
    unsigned int i;
    memset(output_storage, 0xcd, sizeof(output_storage));
    memcpy(source_before, source_storage, sizeof(source_storage));
    assert(dos_vga_pack_words(source, pitch, output, capacity) == -1);
    assert(dos_vga_pack_all(source, pitch, output, capacity) == -1);
    for (i = 0; i < sizeof(output_storage); ++i) assert(output_storage[i] == 0xcd);
    assert(memcmp(source_storage, source_before, sizeof(source_storage)) == 0);
}

static void test_invalid(void)
{
    const unsigned char *source = source_storage + GUARD + 1;
    unsigned char *output = output_storage + GUARD + 3;
    unsigned int i;
    memset(source_storage, 0x7b, sizeof(source_storage));
    for (i = 0; i < DOS_VGA_WIDTH; ++i)
        assert_rejected(source, i, output, UINT_MAX);
    /* Every insufficient capacity, including each plane boundary. */
    memset(output_storage, 0xcd, sizeof(output_storage));
    for (i = 0; i < FRAME_BYTES; ++i) {
        assert(dos_vga_pack_words(source, 512, output, i) == -1);
        assert(dos_vga_pack_all(source, 512, output, i) == -1);
    }
    for (i = 0; i < sizeof(output_storage); ++i) assert(output_storage[i] == 0xcd);
    assert(memcmp(source_storage, source_before, sizeof(source_storage)) == 0);
    assert_rejected(NULL, DOS_VGA_WIDTH, output, FRAME_BYTES);
    assert_rejected(source, DOS_VGA_WIDTH, NULL, FRAME_BYTES);
    assert_rejected(NULL, UINT_MAX, output, UINT_MAX);
    assert_rejected(source, UINT_MAX, output, FRAME_BYTES - 1);
    assert_rejected(NULL, 0, NULL, 0);
    puts("PASS: all 320 short pitches, all 76800 short capacities and null inputs rejected before any write");
}

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
struct bounded_buffer {
    unsigned char *allocation, *data, *forbidden;
    size_t size;
};

/* Place the exact valid extent immediately before an inaccessible page.
 * Detects even discarded overreads, which ordinary canaries cannot detect.
 */
static struct bounded_buffer bounded(size_t length)
{
    struct bounded_buffer result;
    size_t page, readable;
#ifdef _WIN32
    SYSTEM_INFO info;
    DWORD old_protect;
    GetSystemInfo(&info);
    page = info.dwPageSize;
#else
    long page_result = sysconf(_SC_PAGESIZE);
    assert(page_result > 0);
    page = (size_t)page_result;
#endif
    readable = (length + page - 1u) / page * page;
    result.size = readable + page;
#ifdef _WIN32
    result.allocation = VirtualAlloc(NULL, result.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(result.allocation != NULL);
    assert(VirtualProtect(result.allocation + readable, page, PAGE_NOACCESS, &old_protect));
#else
    result.allocation = mmap(NULL, result.size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(result.allocation != MAP_FAILED);
    assert(mprotect(result.allocation + readable, page, PROT_NONE) == 0);
#endif
    result.forbidden = result.allocation + readable;
    result.data = result.forbidden - length;
    return result;
}

static void release(struct bounded_buffer buffer)
{
#ifdef _WIN32
    assert(VirtualFree(buffer.allocation, 0, MEM_RELEASE));
#else
    assert(munmap(buffer.allocation, buffer.size) == 0);
#endif
}

static void test_bounds(void)
{
    unsigned int pitch, x, y, capacity, cases = 0;
    struct bounded_buffer output = bounded(FRAME_BYTES);
    /* End at the actual last pixel, without requiring last-row padding.
     * 239 is odd: these pitches exercise all 16 source alignments at the guard.
     */
    for (pitch = 320; pitch <= 335; ++pitch) {
        size_t length = (size_t)(DOS_VGA_HEIGHT - 1u) * pitch + DOS_VGA_WIDTH;
        struct bounded_buffer input = bounded(length);
        memset(input.data, 0xa5, length);
        for (y = 0; y < DOS_VGA_HEIGHT; ++y)
            for (x = 0; x < DOS_VGA_WIDTH; ++x)
                input.data[(size_t)y * pitch + x] = random_byte();
        compare_all(input.data, pitch, output.data, FRAME_BYTES);
        assert(dos_vga_pack_words(input.forbidden, 319, output.forbidden, UINT_MAX) == -1);
        assert(dos_vga_pack_words(input.forbidden, 512, output.forbidden, FRAME_BYTES - 1) == -1);
        assert(dos_vga_pack_words(NULL, UINT_MAX, output.forbidden, UINT_MAX) == -1);
        assert(dos_vga_pack_words(input.forbidden, UINT_MAX, NULL, UINT_MAX) == -1);
        release(input);
        ++cases;
    }
    /* Large sparse row spacing, well beyond the ordinary test storage. */
    {
        struct bounded_buffer input = bounded((size_t)239u * 65537u + 320u);
        for (y = 0; y < DOS_VGA_HEIGHT; ++y)
            for (x = 0; x < DOS_VGA_WIDTH; ++x)
                input.data[(size_t)y * 65537u + x] = random_byte();
        compare_all(input.data, 65537u, output.data, UINT_MAX);
        release(input);
        ++cases;
    }
    for (capacity = 0; capacity < FRAME_BYTES; ++capacity)
        assert(dos_vga_pack_words(output.forbidden, UINT_MAX,
                                  output.forbidden, capacity) == -1);
    release(output);
    assert(cases == 17);
    puts("PASS: 17 exact source/destination extents with inaccessible guard pages; invalid inputs never access forbidden buffers");
}
#else
static void test_bounds(void)
{
    puts("SKIP: native inaccessible-page API unavailable");
}
#endif

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (strcmp(argv[1], "valid") == 0) test_valid();
    else if (strcmp(argv[1], "invalid") == 0) test_invalid();
    else if (strcmp(argv[1], "bounds") == 0) test_bounds();
    else return 2;
    return 0;
}
