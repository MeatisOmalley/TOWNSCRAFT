#define main inherited_edit_main
#include "edit_test.c"
#undef main

int main(void)
{
    int x=40,y=40,z=40;
    world_alloc(); world_generate(4242); world_stream(40,40,20,100000);
    world_set(x,y,z,B_AIR);
    world_update_dirty_chunks(1000000);
    cache_stream(60,40,1);
    if(!cache_light_pending()) { printf("FAIL: no stream lighting job\n"); return 1; }
    world_set(x,y,z,B_TORCH);
    if(cache_light_pending() || pendingLightEditCount ||
       (world_light_at(x+1,y,z)&15)!=13)
    {
        printf("FAIL: placed torch lighting was deferred\n"); return 1;
    }
    world_set(x,y,z,B_AIR);
    if((world_light_at(x+1,y,z)&15)!=0)
    {
        printf("FAIL: removed torch left block light\n"); return 1;
    }
    printf("PASS: torch placement and removal relight during stream work\n");
    return 0;
}
