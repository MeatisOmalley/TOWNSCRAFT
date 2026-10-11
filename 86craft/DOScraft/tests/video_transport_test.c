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
static unsigned char before[4][65536];
static unsigned int mask=15,readplane,index,start_hi,start_lo,retrace,bytes_written;
static unsigned int chain4,bank,bank_writes;
int __dpmi_int(int n,__dpmi_regs *r) { assert(n==0x10); r->h.al=3; return 0; }
uclock_t uclock(void) { static uclock_t t; return ++t; }
unsigned char inportb(unsigned int p)
{
    if(p==0x3da) return (retrace++&1)?8:0;
    if(p==0x3d5 && index==0x0c) return start_hi;
    if(p==0x3d5 && index==0x0d) return start_lo;
    return 0;
}
void outportb(unsigned int p,unsigned int v) {
    if(p==0x3d4) index=v;
    if(p==0x3cd) { assert(v<4); bank=v; ++bank_writes; }
}
void outportw(unsigned int p,unsigned int v)
{
    if(p==0x3c4 && (v&255)==2) mask=v>>8;
    if(p==0x3c4 && (v&255)==4) chain4=(v>>8)&8;
    if(p==0x3ce && (v&255)==4) readplane=v>>8;
    if(p==0x3d4 && (v&255)==0x0c) start_hi=v>>8;
    if(p==0x3d4 && (v&255)==0x0d) start_lo=v>>8;
}
void dosmemput(const void *s,unsigned int n,unsigned int a)
{
    unsigned int p; assert(a>=0xa0000 && a+n<=0xb0000);
    bytes_written+=n;
    if(chain4) {
        unsigned int i,linear=bank*65536+a-0xa0000;
        const unsigned char *src=s;
        assert(linear+n<=4*65536 && mask==15);
        for(i=0;i<n;++i) vram[(linear+i)&3][(linear+i)>>2]=src[i];
        return;
    }
    assert(bank==0);
    for(p=0;p<4;++p) if(mask&(1u<<p)) memcpy(vram[p]+a-0xa0000,s,n);
}
void dosmemget(unsigned int a,unsigned int n,void *d)
{
    assert(a>=0xa0000 && a+n<=0xb0000 && readplane<4 && !chain4 && !bank);
    memcpy(d,vram[readplane]+a-0xa0000,n);
}
int main(void)
{
    unsigned int frame,y,x,p,pitch,offset,hint;
    video_init();
    for(frame=0;frame<180;++frame) {
        pitch=frame%2?331:512;
        for(y=0;y<240;++y) for(x=0;x<320;++x)
            source[1+y*pitch+x]=(unsigned char)((y<200 && frame%3==0?x/2:x)*13+y*17+(y<200?frame:frame/9));
#ifdef DOS_VGA_PROVEN_VERTICAL
        if(frame%3==0) for(y=0;y<200;y+=2)
            memcpy(source+1+(y+1)*pitch,source+1+y*pitch,320);
#endif
        if(frame%3==0) source[1+110*pitch+127]^=1; /* Sharp one-pixel overlay. */
        hint=frame%3!=2;
#ifdef DOS_ET4000_CHUNKY
        /* Seven flips are not a multiple of three: every physical page
         * crosses access paths, with unchanged and changed HUD bytes. */
        hint=(frame/7)%2==0;
#endif
        dos_video_set_repeated_pixel_hint(hint);
#ifdef DOS_VGA_PROVEN_ROWS
        if(frame%3==0) {
            dos_video_prove_low_rows(0,200);
#ifdef DOS_VGA_PROVEN_VERTICAL
            dos_video_prove_low_row_pairs(0,200);
#endif
            dos_video_unprove_low_rows(110,111); /* One sharp view pixel. */
        }
#endif
        bytes_written=0;
        bank_writes=0;
        memcpy(before,vram,sizeof(before));
        dos_video_present(source+1,pitch);
        assert(!chain4 && !bank);
#ifdef DOS_ET4000_STRIDED
        if(!hint) assert(bank_writes<=3); /* Not a bank switch per scanline. */
#endif
        assert(dos_video_verify(source+1,pitch)==0);
#ifdef DOS_ET4000_CHUNKY
        /* Switching access paths can require refreshing a stale page HUD.
         * Bounds and every actual display pixel below are the oracle. */
        assert(bytes_written<=76800);
        if(!hint) assert(bytes_written>=64000);
#elif defined(DOS_VGA_HUD_CACHE)
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
        for(p=0;p<4;++p) {
            unsigned int page;
            for(page=0;page<3;++page) if(page*DOS_VGA_PLANE_BYTES!=offset)
                assert(!memcmp(vram[p]+page*DOS_VGA_PLANE_BYTES,
                    before[p]+page*DOS_VGA_PLANE_BYTES,DOS_VGA_PLANE_BYTES));
            assert(!memcmp(vram[p]+3*DOS_VGA_PLANE_BYTES,
                before[p]+3*DOS_VGA_PLANE_BYTES,65536-3*DOS_VGA_PLANE_BYTES));
        }
        if(frame%33==0) {
            vram[0][offset+400]^=1;
            assert(dos_video_verify(source+1,pitch)!=0);
            vram[0][offset+400]^=1;
            assert(dos_video_verify(source+1,pitch)==0);
        }
    }
    /* Reinitialization invalidates the shadows in lockstep with VRAM clear. */
    dos_video_restore(); video_init();
    dos_video_present(source+1,pitch);
    assert(dos_video_verify(source+1,pitch)==0);
    dos_video_restore(); puts("PASS: real presenter, all pages/pixels and independent VGA model");
    return 0;
}
