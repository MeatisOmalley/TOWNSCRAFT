#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "raster.h"
#include "graphics_presets.h"
void raster_set_odd_copy_policy(int,int,int,int,int);
void raster_complete_odd_rows(void);
extern u8 dos_very_low_rows[100];
static u8 planes[76800];
unsigned char *dos_video_planar_target(void) { return planes; }
void dos_video_planar_frame(const unsigned char *rows) { (void)rows; }
static u8 reference[512*240+32],candidate[sizeof(reference)],tile[256];
static void draw(u8 *allocation,int potato,int skip,int flat)
{
    u8 *p=allocation+16;
    memset(allocation,0xee,sizeof(reference));
    dos_very_low_mode=1; dos_potato_mode=potato;
    raster_set_odd_copy_policy(skip,1,1,1,1);
    /* A second non-debug case exercises the skipped rows. */
    if(!flat) raster_set_odd_copy_policy(skip,0,0,0,0);
    raster_set_target(p,160,100,512,2);
    raster_fill_rows(0,100,4);
    for(int n=0;n<32;++n) {
        int x=(n*7)%100-10,y=(n*13)%80-5,w=15+n%30,h=8+n%20;
        RVert v[4]={{x*16,y*16,0,0},{(x+w)*16,(y+3)*16,16*65536,0},
            {(x+w-3)*16,(y+h)*16,16*65536,16*65536},{(x-2)*16,(y+h-1)*16,0,16*65536}};
        if(flat || n%3==0) raster_flat_poly(v,4,5+n);
        else raster_poly(v,4,tile,RP_WRAP|(n&1 ? RP_TRANSPARENT : 0));
    }
    raster_finish(); raster_complete_odd_rows();
    for(int i=0;i<16;++i) assert(allocation[i]==0xee);
    for(int i=512*240+16;i<(int)sizeof(reference);++i) assert(allocation[i]==0xee);
}
int main(void)
{
    /* Exercise the actual inline planar sky/fog row fills and their bounds. */
    dos_very_low_mode=dos_potato_mode=0;
    raster_set_odd_copy_policy(1,0,0,0,0);
    memset(planes,0xee,sizeof(planes));
    raster_set_target(candidate+16,160,100,512,2);
    raster_fill_rows(-2,7,19); raster_fill_rows(3,5,31); raster_fill_rows(99,140,43);
    for(int i=0;i<(int)sizeof(planes);++i) {
        int local=i>=38400 ? i-38400 : i,y=local/160,x=local%160;
        int expected=x>=80 || y>=100 ? 0xee : y>=99 ? 43 : y>=3 && y<5 ? 31 : y<7 ? 19 : 0xee;
        assert(planes[i]==expected);
    }
    for(int i=0;i<256;++i) tile[i]=i%11 ? (i%239)+1 : 0;
    for(int skip=0;skip<=1;++skip) for(int flat=0;flat<=1;++flat) {
        draw(reference,0,skip,flat); draw(candidate,1,skip,flat);
        int repeated=0;
        for(int y=0;y<100;++y) {
            int owner=dos_very_low_rows[y]&2 ? y-1 : y;
            repeated+=owner!=y;
            for(int x=0;x<320;x+=4) {
                assert(candidate[16+y*1024+x]==reference[16+owner*1024+x]);
                assert(candidate[16+(y*2+1)*512+x]==candidate[16+y*1024+x]);
            }
        }
        assert(repeated==50);
        if(!skip || flat) for(int y=0;y<(!skip ? 200 : 96);++y) for(int x=0;x<320;x+=4)
            for(int dx=1;dx<4;++dx) assert(candidate[16+y*512+x]==candidate[16+y*512+x+dx]);
    }
    dos_resolution_set(DOS_RES_LOW);
    raster_set_odd_copy_policy(1,0,0,0,0);
    for(int y=0;y<100;++y) assert(!(dos_very_low_rows[y]&2));
    puts("PASS: real potato textured/transparent/flat raster, protected bands, weather fallback and mode reset");
    return 0;
}
