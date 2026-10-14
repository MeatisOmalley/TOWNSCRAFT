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
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED)
static unsigned char menu_source[sizeof(source)];
#endif
static unsigned char before[4][65536];
#ifdef DOS_LOW_PLANAR
static unsigned char saved_source[sizeof(source)];
static unsigned char planar_rows[100];
#endif
static unsigned int mask=15,readplane,index,start_hi,start_lo,retrace,bytes_written;
static unsigned int vsync_reads,start_lo_writes;
static uclock_t test_clock;
static unsigned int chain4,bank,bank_writes;
/* ET4000 registers the presenter's card probe reads back. */
static unsigned int reg3cd,crtc33;
/* PIT channel 2 (mode 2, modulo 65536) and the 100 Hz tick, in PIT counts. */
static unsigned int pit_now,pit2_base,pit2_latch,pit2_byte;
#ifdef DOS_VGA_ASYNC_FLIP
static volatile unsigned int test_ticks;
static void pit_advance(unsigned int counts) { pit_now+=counts; test_ticks=pit_now/11932u; }
#endif
#ifdef DOS_VERY_LOW
int dos_very_low_mode=1;
#endif
int __dpmi_int(int n,__dpmi_regs *r) { assert(n==0x10); r->h.al=3; return 0; }
uclock_t uclock(void) { return ++test_clock; }
unsigned char inportb(unsigned int p)
{
    if(p==0x3da) { ++vsync_reads; return (retrace++&1)?8:0; }
    if(p==0x3d5 && index==0x0c) return start_hi;
    if(p==0x3d5 && index==0x0d) return start_lo;
    if(p==0x3d5 && index==0x33) return crtc33;
    if(p==0x3cd) return reg3cd;
    if(p==0x42) return (pit2_latch>>(8*(pit2_byte++&1)))&255;
    return 0;
}
void outportb(unsigned int p,unsigned int v) {
    if(p==0x3d4) index=v;
    if(p==0x43 && v==0xb4) pit2_base=pit_now;
    if(p==0x43 && v==0x80) { pit2_latch=(pit2_base-pit_now)&65535u; pit2_byte=0; }
    if(p==0x3d5 && index==0x33) crtc33=v&15;
    if(p==0x3cd) {
        reg3cd=v;
        if(v==0x55 || v==0xaa) return; /* Card probe patterns. */
        assert(v<4); bank=v; ++bank_writes;
    }
}
void outportw(unsigned int p,unsigned int v)
{
    if(p==0x3c4 && (v&255)==2) mask=v>>8;
    if(p==0x3c4 && (v&255)==4) chain4=(v>>8)&8;
    if(p==0x3ce && (v&255)==4) readplane=v>>8;
    if(p==0x3d4 && (v&255)==0x0c) start_hi=v>>8;
    if(p==0x3d4 && (v&255)==0x0d) { start_lo=v>>8; ++start_lo_writes; }
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
#ifdef DOS_VGA_ASYNC_FLIP
    unsigned int reads_before;
#endif
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
#ifdef DOS_VGA_HUD_PROOF
        if(frame%9>=3 && hint && frame%3==0)
            dos_video_prove_hud_unchanged((frame+1)%3);
#endif
#ifdef DOS_VGA_PROVEN_ROWS
#ifdef DOS_LOW_PLANAR
        if(frame%3==0 && frame%6!=0) {
#else
        if(frame%3==0) {
#endif
            dos_video_prove_low_rows(0,200);
#ifdef DOS_VGA_PROVEN_VERTICAL
            dos_video_prove_low_row_pairs(0,200);
#endif
            dos_video_unprove_low_rows(110,111); /* One sharp view pixel. */
        }
#endif
#ifdef DOS_LOW_PLANAR
        /* The renderer's planar rows, then scrambled source rows: correct
         * VRAM proves those rows came from the plane buffer. Logical row 55
         * (rows 110/111, the sharp pixel) stays chunky; frames divisible by
         * six carry no row proof at all. */
        memcpy(saved_source,source,sizeof(source));
        if(frame%3==0 && hint) {
            unsigned char *planes=dos_video_planar_target();
            for(y=0;y<100;++y) {
                planar_rows[y]=y!=55;
                for(x=0;x<160;++x)
                    planes[y*160+(x&1)*38400+(x>>1)]=source[1+2*y*pitch+2*x];
                if(planar_rows[y]) for(x=0;x<320;++x) {
                    source[1+2*y*pitch+x]^=0x5a;
                    source[1+(2*y+1)*pitch+x]^=0xa5;
                }
            }
            dos_video_planar_frame(planar_rows);
        }
#endif
        bytes_written=0;
        bank_writes=0;
        memcpy(before,vram,sizeof(before));
#ifdef DOS_VGA_ASYNC_FLIP
        if(frame%4==1) test_clock+=UCLOCKS_PER_SEC/30;
        reads_before=vsync_reads;
#endif
        dos_video_present(source+1,pitch);
#ifdef DOS_LOW_PLANAR
        memcpy(source,saved_source,sizeof(source));
#endif
#ifdef DOS_VGA_ASYNC_FLIP
        assert(start_lo_writes==1);
        /* Wait only when neither of the last two gaps spans a refresh. */
        if(frame>=2 && (frame%4==3 || frame%4==0)) assert(vsync_reads>reads_before);
        else assert(vsync_reads==reads_before);
#endif
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
        for(p=0;p<4;++p) for(y=0;y<240;++y) for(x=0;x<80;++x) {
            unsigned int expected=source[1+y*pitch+x*4+p];
#ifdef DOS_VERY_LOW
            if(frame%3==0 && hint && y<200 && y!=110) {
                unsigned int source_row=(y&1u) && y!=111 ? y-1 : y;
                expected=source[1+source_row*pitch+x*4];
            }
#endif
            assert(vram[p][offset+y*80+x]==expected);
        }
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
#if defined(DOS_ET4000_CHUNKY) && defined(DOS_ET4000_STRIDED)
    /* Inventory overlays are sharp even when the underlying Low world used
     * repeated pixels. One normal upload establishes its displayed page;
     * thereafter a still menu writes zero bytes and cursor motion only rows. */
    {
        unsigned int menu_start,reads_before;
        dos_video_set_repeated_pixel_hint(1);
#ifdef DOS_VGA_PROVEN_ROWS
        dos_video_prove_low_rows(0,200); /* Stale world proof must be ignored. */
#endif
        dos_video_set_inventory_hint();
        bytes_written=0;
        dos_video_present(source+1,pitch);
        menu_start=start_hi<<8|start_lo;
        assert(bytes_written>=64000 && bytes_written<=76800);
        assert(dos_video_verify(source+1,pitch)==0);
        memcpy(menu_source,source,sizeof(source));
        dos_video_set_repeated_pixel_hint(1);
        dos_video_set_inventory_hint();
        bytes_written=0;
        reads_before=vsync_reads;
        dos_video_present(menu_source+1,pitch);
        assert(bytes_written==0 && vsync_reads==reads_before);
        assert((start_hi<<8|start_lo)==menu_start);
        source[1+100*pitch+15]^=1;
        source[1+120*pitch+55]^=1;
        dos_video_set_repeated_pixel_hint(1);
        dos_video_set_inventory_hint();
        bytes_written=0;
        dos_video_present(source+1,pitch);
        assert(bytes_written==640);
        assert((start_hi<<8|start_lo)==menu_start);
        assert(dos_video_verify(source+1,pitch)==0);
        dos_video_set_repeated_pixel_hint(0);
        dos_video_present(source+1,pitch);
        assert((start_hi<<8|start_lo)!=menu_start);
        assert(dos_video_verify(source+1,pitch)==0);
        memcpy(menu_source,source,sizeof(source));
        assert(dos_video_inventory_canvas_save(source+1,pitch));
        menu_source[1+120*pitch+100]^=1; /* Previous cursor inside panel. */
        menu_source[1+2*pitch+20]^=1;   /* Outside the panel is untouched. */
        assert(dos_video_inventory_canvas_restore(menu_source+1,pitch));
        assert(menu_source[1+120*pitch+100]==source[1+120*pitch+100]);
        assert(menu_source[1+2*pitch+20]!=source[1+2*pitch+20]);
    }
#endif
    /* Reinitialization invalidates the shadows in lockstep with VRAM clear. */
    dos_video_restore(); video_init();
    dos_video_present(source+1,pitch);
    assert(dos_video_verify(source+1,pitch)==0);
#ifdef DOS_VGA_ASYNC_FLIP
    /* Sub-tick page-reuse guard with the 100 Hz clock: wait only while the
     * second most recent flip is under 17.5 ms old, across tick phases and
     * channel-2 wraps. */
    {
        static const unsigned int gap_us[]={0,0,8000,8000,9600,7800,30000,2000,15000,2600,54000,70000,1000,16000,19000};
        static const unsigned int must_wait[]={0,0,1,1,0,1,0,0,1,0,0,0,0,1,0};
        dos_video_restore(); video_init();
        dos_video_set_tick_clock(&test_ticks,100);
        for(frame=0;frame<sizeof(gap_us)/sizeof(gap_us[0]);++frame) {
            pit_advance((unsigned int)(gap_us[frame]*1193182ull/1000000u));
            reads_before=vsync_reads;
            dos_video_present(source+1,pitch);
            assert((vsync_reads>reads_before)==must_wait[frame]);
        }
        dos_video_set_tick_clock(0,0);
    }
#endif
    dos_video_restore(); puts("PASS: real presenter, all pages/pixels and independent VGA model");
    return 0;
}
