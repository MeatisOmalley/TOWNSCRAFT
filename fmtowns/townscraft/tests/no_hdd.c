/* Legacy native fixtures have no hardware controller. */
#include "hdd.h"
int hdd_init(void) { return 0; }
u32 hdd_sector_count(void) { return 0; }
int hdd_transfer(u32 lba,u32 count,int write,u8 *buffer) { return -1; }
int hdd_transfer_sync(u32 lba,u32 count,int write,u8 *buffer) { return -1; }
void hdd_cancel(void) {}
