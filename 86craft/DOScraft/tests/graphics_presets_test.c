/* Independent expected policy and imported-header enum checks. Host only. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "render.h"
#include "graphics_presets.h"

static void check_settings(int index, int scale, int view, int flat)
{
    const DosGraphicsSettings *s=dos_graphics_settings(index);
    assert(s->render_scale==scale);
    assert(s->view_distance==view);
    assert(s->flat_distance==flat);
    assert(s->distance_mode==0);
    assert(s->texture_subdivision==SUBDIV_ADAPTIVE);
}

int main(void)
{
    static const int invalid[]={INT_MIN,-100,-1,4,5,100,INT_MAX};
    static const char *const names[]={"Graphics: Low","Graphics: Medium","Graphics: High","Graphics: Very Low"};
    int i,index=DOS_GRAPHICS_DEFAULT;
    assert(DOS_GRAPHICS_COUNT==4 && DOS_GRAPHICS_DEFAULT==DOS_GRAPHICS_LOW);
    check_settings(DOS_GRAPHICS_LOW,2,8,0);
    check_settings(DOS_GRAPHICS_MEDIUM,2,8,0);
    check_settings(DOS_GRAPHICS_HIGH,1,8,0);
    check_settings(DOS_GRAPHICS_VERY_LOW,2,8,0);
    for(i=0;i<300;++i) {
        assert(index==i%4 && dos_graphics_index(index)==index);
        assert(!strcmp(dos_graphics_message(index),names[i%4]));
        index=dos_graphics_next(index);
    }
    for(i=0;i<(int)(sizeof(invalid)/sizeof(invalid[0]));++i) {
        assert(dos_graphics_index(invalid[i])==DOS_GRAPHICS_LOW);
        assert(dos_graphics_next(invalid[i])==DOS_GRAPHICS_LOW);
        assert(dos_graphics_settings(invalid[i])==dos_graphics_settings(DOS_GRAPHICS_LOW));
        assert(!strcmp(dos_graphics_message(invalid[i]),names[0]));
    }
    for(i=0;i<DOS_RES_COUNT;++i) {
        int scale=dos_resolution_set(i);
        assert(dos_resolution_index(scale)==i);
        assert(dos_potato_mode==(i==DOS_RES_POTATO));
        assert(dos_very_low_mode==(i>=DOS_RES_VERY_LOW));
        scale=dos_resolution_change(scale,1);
        assert(dos_resolution_index(scale)==(i+1)%DOS_RES_COUNT);
        scale=dos_resolution_change(scale,-1);
        assert(dos_resolution_index(scale)==i);
    }
    puts("PASS: pure graphics policy, bounds, cycles, imported adaptive enum");
    return 0;
}
