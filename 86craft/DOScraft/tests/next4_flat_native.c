#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "raster.h"
#include "graphics_presets.h"
int dos_cpu386;
static u8 planes[76800+32],frame[512*240+32];
unsigned char *dos_video_planar_target(void) { return planes+16; }
void dos_video_planar_frame(const unsigned char *rows) { (void)rows; }
void raster_set_odd_copy_policy(int,int,int,int,int);
void raster_complete_odd_rows(void);
int main(void) {
    u32 nonPotatoHash=2166136261u;
    for(int potato=0;potato<2;++potato) for(int sharp=0;sharp<2;++sharp) {
        memset(planes,0xee,sizeof(planes)); memset(frame,0xee,sizeof(frame));
        dos_very_low_mode=1; dos_potato_mode=potato;
        raster_set_odd_copy_policy(1,sharp,sharp,sharp,sharp);
        raster_set_target(frame+16,160,100,512,2);
        raster_fill_rows(0,100,4);
        RVert v[4]={{20*16,20*16,0,0},{100*16,20*16,0,0},
            {100*16,80*16,0,0},{20*16,80*16,0,0}};
        raster_flat_poly(v,4,23);
        assert(raster_width()==160 && raster_height()==100);
        raster_finish(); raster_complete_odd_rows();
        assert(frame[16+100*512+100]==23 && frame[16+10*512+10]==4);
        for(int i=0;i<16;++i) assert(planes[i]==0xee && planes[76800+16+i]==0xee &&
            frame[i]==0xee && frame[512*240+16+i]==0xee);
        if(!potato) for(int i=0;i<512*200;++i) nonPotatoHash=(nonPotatoHash^frame[16+i])*16777619u;
        /* Exercise clipped, sloped and tiny polygons plus state restoration. */
        raster_set_odd_copy_policy(1,0,0,0,0);
        raster_set_target(frame+16,160,100,512,2);
        for(int k=0;k<128;++k) {
            int x=(k*17)%180-20,y=(k*13)%120-20;
            RVert p[4]={{x*16,y*16,0,0},{(x+37)*16,(y+4)*16,0,0},
                {(x+30)*16,(y+29)*16,0,0},{(x-4)*16,(y+22)*16,0,0}};
            raster_flat_poly(p,4,17+k);
        }
        raster_finish(); raster_complete_odd_rows();
        for(int i=0;i<16;++i) assert(planes[i]==0xee && planes[76800+16+i]==0xee &&
            frame[i]==0xee && frame[512*240+16+i]==0xee);
    }
    printf("NONPOTATO_HASH=%08x\nPASS: native flat spans, sharp bands, clipping, guards and restored dimensions\n",nonPotatoHash);
    return 0;
}
