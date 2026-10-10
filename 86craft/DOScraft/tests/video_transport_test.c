/* Execute the real presenter against an independent four-plane VGA model.
 * Complements native pack tests and actual 86Box readback, no speed claim. */
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <dpmi.h>
#include <time.h>
#include "video_backend.h"
#include "vga_pack.h"
static unsigned char vram[4][65536],source[512*240+4];
static unsigned int mask=15,readplane,index,start_hi,start_lo,retrace,bytes_written;
int __dpmi_int(int n,__dpmi_regs *r) { assert(n==0x10); r->h.al=3; return 0; }
uclock_t uclock(void) { static uclock_t t; return ++t; }
unsigned char inportb(unsigned int p)
{
    if(p==0x3da) return (retrace++&1)?8:0;
    if(p==0x3d5 && index==0x0c) return start_hi;
    if(p==0x3d5 && index==0x0d) return start_lo;
    return 0;
}
void outportb(unsigned int p,unsigned int v) { if(p==0x3d4) index=v; }
void outportw(unsigned int p,unsigned int v)
{
    if(p==0x3c4 && (v&255)==2) mask=v>>8;
    if(p==0x3ce && (v&255)==4) readplane=v>>8;
    if(p==0x3d4 && (v&255)==0x0c) start_hi=v>>8;
    if(p==0x3d4 && (v&255)==0x0d) start_lo=v>>8;
}
void dosmemput(const void *s,unsigned int n,unsigned int a)
{
    unsigned int p; assert(a>=0xa0000 && a+n<=0xb0000);
    bytes_written+=n;
    for(p=0;p<4;++p) if(mask&(1u<<p)) memcpy(vram[p]+a-0xa0000,s,n);
}
void dosmemget(unsigned int a,unsigned int n,void *d)
{
    assert(a>=0xa0000 && a+n<=0xb0000 && readplane<4);
    memcpy(d,vram[readplane]+a-0xa0000,n);
}
int main(void)
{
    unsigned int frame,y,x,p,pitch,offset;
    video_init();
    for(frame=0;frame<36;++frame) {
        pitch=frame%2?331:512;
        for(y=0;y<240;++y) for(x=0;x<320;++x)
            source[1+y*pitch+x]=(unsigned char)((y<200 && frame%3==0?x/2:x)*13+y*17+(y<200?frame:frame/9));
        if(frame%3==0) source[1+110*pitch+127]^=1; /* Sharp one-pixel overlay. */
        dos_video_set_repeated_pixel_hint(frame%3!=2);
        bytes_written=0;
        dos_video_present(source+1,pitch);
        assert(dos_video_verify(source+1,pitch)==0);
#ifdef DOS_VGA_HUD_CACHE
#ifdef DOS_VGA_BROADCAST
        if(frame%3==0) assert(bytes_written<(frame%9>=3?64000u:76800u));
        else assert(bytes_written==(frame%9>=3?64000u:76800u));
#else
        assert(bytes_written==(frame%9>=3?64000u:76800u));
#endif
#else
        assert(bytes_written==76800);
#endif
        offset=((frame+1)%3)*DOS_VGA_PLANE_BYTES;
        assert((start_hi<<8|start_lo)==offset);
        for(p=0;p<4;++p) for(y=0;y<240;++y) for(x=0;x<80;++x)
            assert(vram[p][offset+y*80+x]==source[1+y*pitch+x*4+p]);
    }
    /* Reinitialization invalidates the shadows in lockstep with VRAM clear. */
    dos_video_restore(); video_init();
    dos_video_present(source+1,pitch);
    assert(dos_video_verify(source+1,pitch)==0);
    dos_video_restore(); puts("PASS: real presenter, all pages/pixels and independent VGA model");
    return 0;
}
