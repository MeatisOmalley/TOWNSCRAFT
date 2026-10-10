/* Keep checks active even when an optimized toolchain defines NDEBUG. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "vga_hud_cache.h"

#define GUARD 32u
#define OFFSETS 16u
#define STORAGE_BYTES (GUARD + OFFSETS + DOS_VGA_HUD_CACHE_MAX_BYTES + GUARD)
static unsigned char source_store[STORAGE_BYTES], shadow_store[STORAGE_BYTES];
static unsigned char source_before[STORAGE_BYTES], shadow_before[STORAGE_BYTES];
static uint32_t random_state = UINT32_C(0x3648f291);

static unsigned char random_byte(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return (unsigned char)(random_state >> 24);
}

/* Independent host/endian-neutral oracle: only individual byte comparisons. */
static int byte_reference(const unsigned char *source, const unsigned char *shadow,
                           unsigned int bytes)
{
    unsigned int i;
    int different = 0;
    for (i = 0; i < bytes; ++i)
        if (source[i] != shadow[i]) different = 1;
    return different;
}

static void check_call(unsigned int source_offset, unsigned int shadow_offset,
                        unsigned int bytes)
{
    unsigned int i, source_start = GUARD + source_offset;
    unsigned int shadow_start = GUARD + shadow_offset;
    int expected = byte_reference(source_store + source_start, shadow_store + shadow_start, bytes);
    memcpy(source_before, source_store, sizeof(source_store));
    memcpy(shadow_before, shadow_store, sizeof(shadow_store));
    assert(dos_vga_hud_changed(source_store + source_start,
                               shadow_store + shadow_start, bytes) == expected);
    assert(memcmp(source_store, source_before, sizeof(source_store)) == 0);
    /* Check the exact update and every byte outside it, including partial-word
     * tails, padding within the allocation, and guards on both sides. */
    for (i = 0; i < sizeof(shadow_store); ++i) {
        unsigned char wanted = shadow_before[i];
        if (expected && i >= shadow_start && i - shadow_start < bytes)
            wanted = source_before[source_start + i - shadow_start];
        assert(shadow_store[i] == wanted);
    }
}

static void prepare(unsigned int source_offset, unsigned int shadow_offset,
                     unsigned int bytes)
{
    unsigned int i;
    /* Different surrounding bytes also detect comparisons beyond bytes. */
    memset(source_store, 0xa5, sizeof(source_store));
    memset(shadow_store, 0xcd, sizeof(shadow_store));
    for (i = 0; i < bytes; ++i) {
        unsigned char value = random_byte();
        source_store[GUARD + source_offset + i] = value;
        shadow_store[GUARD + shadow_offset + i] = value;
    }
}

static void test_alignments_and_lengths(void)
{
    static const unsigned int lengths[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 3197, 3198, 3199, 3200
    };
    unsigned int s, d, l, cases = 0;
    for (s = 0; s < OFFSETS; ++s)
        for (d = 0; d < OFFSETS; ++d)
            for (l = 0; l < sizeof(lengths) / sizeof(lengths[0]); ++l) {
                unsigned int bytes = lengths[l];
                prepare(s, d, bytes);
                check_call(s, d, bytes);
                if (bytes) {
                    source_store[GUARD + s] ^= 1u;
                    check_call(s, d, bytes);
                    check_call(s, d, bytes); /* Updated shadow is now equal. */
                    source_store[GUARD + s + bytes - 1] ^= 0x80u;
                    check_call(s, d, bytes);
                    check_call(s, d, bytes);
                }
                ++cases;
            }
    assert(cases == 4864);
    puts("PASS: 4864 alignment/length combinations, byte oracle, exact updates and guards");
}

static void test_every_bit(void)
{
    unsigned int byte, bit, cases = 0;
    prepare(1, 3, DOS_VGA_HUD_CACHE_MAX_BYTES);
    check_call(1, 3, DOS_VGA_HUD_CACHE_MAX_BYTES);
    for (byte = 0; byte < DOS_VGA_HUD_CACHE_MAX_BYTES; ++byte)
        for (bit = 0; bit < 8; ++bit) {
            source_store[GUARD + 1 + byte] ^= (unsigned char)(1u << bit);
            check_call(1, 3, DOS_VGA_HUD_CACHE_MAX_BYTES);
            check_call(1, 3, DOS_VGA_HUD_CACHE_MAX_BYTES);
            ++cases;
        }
    assert(cases == 25600);
    puts("PASS: all 25600 single-bit changes at every HUD byte, followed by equality");
}

static void check_invalid(const unsigned char *source, unsigned char *shadow,
                           unsigned int bytes)
{
    memcpy(source_before, source_store, sizeof(source_store));
    memcpy(shadow_before, shadow_store, sizeof(shadow_store));
    assert(dos_vga_hud_changed(source, shadow, bytes) == -1);
    assert(memcmp(source_store, source_before, sizeof(source_store)) == 0);
    assert(memcmp(shadow_store, shadow_before, sizeof(shadow_store)) == 0);
}

static void test_invalid(void)
{
    unsigned char *source = source_store + GUARD + 1;
    unsigned char *shadow = shadow_store + GUARD + 3;
    prepare(1, 3, DOS_VGA_HUD_CACHE_MAX_BYTES);
    check_invalid(NULL, shadow, 0);
    check_invalid(source, NULL, 0);
    check_invalid(NULL, NULL, 0);
    check_invalid(NULL, shadow, DOS_VGA_HUD_CACHE_MAX_BYTES);
    check_invalid(source, NULL, DOS_VGA_HUD_CACHE_MAX_BYTES);
    check_invalid(source, shadow, DOS_VGA_HUD_CACHE_MAX_BYTES + 1);
    check_invalid(source, shadow, UINT_MAX);
    check_call(1, 3, 0);
    memcpy(shadow_before, shadow_store, sizeof(shadow_store));
    assert(dos_vga_hud_changed(shadow, shadow, DOS_VGA_HUD_CACHE_MAX_BYTES) == 0);
    assert(memcmp(shadow_store, shadow_before, sizeof(shadow_store)) == 0);
    puts("PASS: NULL, bounded length, zero-length and identical-pointer semantics");
}

typedef struct { unsigned char *base; size_t page; } Guarded;

static Guarded guarded_alloc(void)
{
    Guarded buffer;
#ifdef _WIN32
    SYSTEM_INFO info;
    DWORD old;
    GetSystemInfo(&info);
    buffer.page = info.dwPageSize;
    buffer.base = VirtualAlloc(NULL, buffer.page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(buffer.base);
    assert(VirtualProtect(buffer.base + buffer.page, buffer.page, PAGE_NOACCESS, &old));
#else
    long page = sysconf(_SC_PAGESIZE);
    assert(page > 0);
    buffer.page = (size_t)page;
    buffer.base = mmap(NULL, buffer.page * 2, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(buffer.base != MAP_FAILED);
    assert(mprotect(buffer.base + buffer.page, buffer.page, PROT_NONE) == 0);
#endif
    assert(buffer.page >= DOS_VGA_HUD_CACHE_MAX_BYTES);
    return buffer;
}

static void guarded_writable(Guarded buffer, int writable)
{
#ifdef _WIN32
    DWORD old;
    assert(VirtualProtect(buffer.base, buffer.page,
                          writable ? PAGE_READWRITE : PAGE_READONLY, &old));
#else
    assert(mprotect(buffer.base, buffer.page, PROT_READ | (writable ? PROT_WRITE : 0)) == 0);
#endif
}

static void guarded_free(Guarded buffer)
{
#ifdef _WIN32
    assert(VirtualFree(buffer.base, 0, MEM_RELEASE));
#else
    assert(munmap(buffer.base, buffer.page * 2) == 0);
#endif
}

static void test_protected_pages(void)
{
    static const unsigned int lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 3197, 3198, 3199, 3200};
    Guarded source = guarded_alloc(), shadow = guarded_alloc();
    unsigned int l, i;
    for (l = 0; l < sizeof(lengths) / sizeof(lengths[0]); ++l) {
        unsigned int bytes = lengths[l];
        unsigned char *s = source.base + source.page - bytes;
        unsigned char *d = shadow.base + shadow.page - bytes;
        guarded_writable(source, 1);
        guarded_writable(shadow, 1);
        for (i = 0; i < bytes; ++i) s[i] = d[i] = random_byte();
        guarded_writable(source, 0);
        guarded_writable(shadow, 0);
        /* A write on equality or a read past the partial word faults here.
         * For zero length, both pointers are directly in no-access pages. */
        assert(dos_vga_hud_changed(s, d, bytes) == 0);
        assert(dos_vga_hud_changed(NULL, d, bytes) == -1);
        if (bytes) {
            guarded_writable(source, 1);
            s[bytes - 1] ^= 1u;
            guarded_writable(source, 0);
            guarded_writable(shadow, 1);
            assert(dos_vga_hud_changed(s, d, bytes) == 1);
            assert(byte_reference(s, d, bytes) == 0);
            guarded_writable(shadow, 0);
            assert(dos_vga_hud_changed(s, d, bytes) == 0);
        }
    }
    /* Oversized lengths reject before touching inaccessible non-NULL buffers. */
    assert(dos_vga_hud_changed(source.base + source.page, shadow.base + shadow.page,
                               DOS_VGA_HUD_CACHE_MAX_BYTES + 1) == -1);
    guarded_free(source);
    guarded_free(shadow);
    puts("PASS: protected tails, read-only equality and zero-length no-access pages");
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (strcmp(argv[1], "matrix") == 0) test_alignments_and_lengths();
    else if (strcmp(argv[1], "bits") == 0) test_every_bit();
    else if (strcmp(argv[1], "invalid") == 0) test_invalid();
    else if (strcmp(argv[1], "pages") == 0) test_protected_pages();
    else return 2;
    return 0;
}
