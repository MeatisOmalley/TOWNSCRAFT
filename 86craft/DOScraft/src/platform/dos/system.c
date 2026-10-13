/* DOS startup backing. g_ramMB is installed RAM, not arena capacity; the
 * imported world code selects its smaller world sizes from that value. */
#include "sys.h"
#include "system.h"
#include "heap.h"
#include "irq.h"
#include "input.h"
#include "video_backend.h"
#include <dpmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MIB 1048576u
#define LOW_BYTES MIB
#define HIGH_BYTES (8u * MIB)
u32 g_ramMB;
static void *backing;
static int initialized, cleanup_registered;

void dos_system_shutdown(void)
{
    dos_input_shutdown();
    dos_video_set_tick_clock(0, 0);
    dos_irq_shutdown(); /* No ISR may touch arenas after they are released. */
    if (initialized) dos_heap_shutdown();
    initialized = 0;
    free(backing);
    backing = 0;
}

void fatal(const char *message)
{
    dos_irq_shutdown();
    dos_video_restore();
    fprintf(stderr, "DOScraft: %s\n", message);
    exit(1); /* DOS teardown replaces the bare-metal permanent halt. */
}

void sys_init(void)
{
    __dpmi_version_ret version;
    __dpmi_free_mem_info memory;
    __dpmi_regs regs = {0};
    unsigned char *aligned;
    unsigned int low_bytes, high_bytes;
    if (initialized) fatal("System already initialized");
    if (__dpmi_get_version(&version) || version.cpu < 4)
        fatal("This build requires a 486 CPU");
    /* BIOS AH=88 gives installed extended KiB on this pinned ISA-486 BIOS.
     * Add the conventional/address-hole MiB, as in the Towns installed-RAM
     * register. Do not infer installed RAM from DPMI's currently free pages. */
    regs.h.ah = 0x88;
    if (__dpmi_int(0x15, &regs) || (regs.x.flags & 1) || regs.x.ax < 1024u)
        fatal("DOScraft requires at least 2 MiB RAM");
    g_ramMB = 1u + regs.x.ax / 1024u;
    if (__dpmi_get_free_memory_information(&memory) || memory.size_of_paging_file_partition_in_pages)
        fatal("Start CWSDPMI with paging disabled (-s-)");
    /* Keep permanent low allocations independent of high temporary rewind.
     * Reserve less backing on smaller machines; the imported world already
     * selects 96/160/208/256-block widths from installed RAM. */
    if (g_ramMB >= 16) { low_bytes=LOW_BYTES; high_bytes=HIGH_BYTES; }
    else if (g_ramMB >= 8) {
        low_bytes=672u*1024u;
#ifdef DOS_LARGE_WORLD
        /* Prebaked terrain avoids the 3.2 MiB whole-world generation stage. */
        high_bytes=3072u*1024u;
#else
        high_bytes=4864u*1024u;
#endif
    }
    else if (g_ramMB >= 4) { low_bytes=640u*1024u; high_bytes=2560u*1024u; }
    else { low_bytes=448u*1024u; high_bytes=896u*1024u; }
    backing = malloc(low_bytes + high_bytes + 15);
    if (!backing) fatal("Cannot allocate DOS game arenas");
    aligned = (unsigned char *)(((uintptr_t)backing + 15u) & ~(uintptr_t)15u);
    if (dos_heap_init(aligned, low_bytes, aligned + low_bytes, high_bytes))
        fatal("Cannot bind DOS game arenas");
    initialized = 1;
    if (!cleanup_registered) {
        if (atexit(dos_system_shutdown)) fatal("Cannot register DOS cleanup");
        cleanup_registered = 1;
    }
    if (dos_irq_init()) fatal("Cannot install resident DOS interrupts");
    dos_video_set_tick_clock(&g_ticks, TICKS_PER_SEC);
    dos_input_init(); /* Missing driver is a supported keyboard-only startup. */
}
