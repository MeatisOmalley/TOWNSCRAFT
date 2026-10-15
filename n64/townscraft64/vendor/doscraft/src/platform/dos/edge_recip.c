#include "edge_recip.h"

uint32_t g_dos_edge_recip[DOS_EDGE_RECIP_MAX_DY + 1];

void dos_edge_recip_init(void)
{
    static int initialized;
    unsigned int dy;
    if (initialized) return;
    /* Entries 0/1 stay zero: neither is read by the multiply path. */
    for (dy = 2; dy <= DOS_EDGE_RECIP_MAX_DY; ++dy)
        g_dos_edge_recip[dy] = UINT_MAX / dy + 1u;
    initialized = 1;
}
