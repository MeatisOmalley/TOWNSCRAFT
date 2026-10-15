/* Townscraft64 entry: bring up the N64 platform, then run DOScraft's game
 * (the pinned Towns kmain with DOScraft's overlays). */
#include <libdragon.h>

void kmain(void);
void n64_platform_init(void);

int main(void)
{
    n64_platform_init();
    kmain();
    return 0;
}
