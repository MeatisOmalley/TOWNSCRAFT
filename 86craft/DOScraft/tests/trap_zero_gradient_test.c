/* Direct actual assembly parity: zero/nonzero shifted deltas, signed rounding,
 * widths/clipping, skip rows and independent output/Trap-state guards. */
#include "raster.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    u8 *row; int stride,rows,lx,ldx,lu,ldu,lv,ldv;
    int rx,rdx,ru,rdu,rv,rdv; const u8 *tile; int maxX; u32 pixels;
} Trap;
#define DECLARE(n) void n(Trap *); void reference_##n(Trap *);
DECLARE(trap_opaque1) DECLARE(trap_opaque2) DECLARE(trap_transp1) DECLARE(trap_transp2)
DECLARE(trap_opaque1w) DECLARE(trap_opaque2w) DECLARE(trap_transp1w) DECLARE(trap_transp2w)
static void (*old[8])(Trap *)={reference_trap_opaque1,reference_trap_opaque2,
    reference_trap_transp1,reference_trap_transp2,reference_trap_opaque1w,
    reference_trap_opaque2w,reference_trap_transp1w,reference_trap_transp2w};
static void (*current[8])(Trap *)={trap_opaque1,trap_opaque2,trap_transp1,trap_transp2,
    trap_opaque1w,trap_opaque2w,trap_transp1w,trap_transp2w};
static u32 seed=0xa172852u;
static u32 next(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
int main(void)
{
    u8 a[512*80+32],b[sizeof(a)],*allocation=malloc(65536),*tile;
    int k,i,j;
    if(!allocation || sizeof(Trap)!=72) return 1;
    tile=(u8 *)(((uintptr_t)allocation+16383)&~(uintptr_t)4095);
    for(i=0;i<65536;++i) allocation[i]=(i%7)?(u8)(i*43+17):0;
    /* raster_set_target initializes both original reciprocal tables. */
    extern void reference_raster_set_target(u8 *,int,int,int,int);
    for(k=0;k<8;++k) for(i=0;i<2000;++i) {
        int scale=1+(k&1),maxX=320/scale,x=(k>=4)?(int)(next()%400)-100:0;
        int width=1+next()%400;
        Trap t={a+16,512,1+(int)(next()%64),x*65536,0,0,0,0,0,
            (x+width)*65536,0,0,0,0,0,tile+(i%16)*16,maxX,0},r;
        if(i==0) t.rows=0;
        if(k<4) {
            t.rx=width*65536; t.ru=(i&1)?15*65536-1:255;
            t.lv=t.rv=(i&2)?8*65536:0;
            if(i&4) { t.lu=t.ru; t.ru=0; }
        } else {
            t.ldx=(int)(next()%65537)-32768; t.rdx=(int)(next()%65537)-32768;
            t.lu=(int)(next()%(16*65536)); t.lv=(int)(next()%(16*65536));
            t.ru=t.lu+((i&1)?(int)(next()%(32*65536))-16*65536:next()%256);
            t.rv=t.lv+((i&2)?(int)(next()%(32*65536))-16*65536:next()%256);
            t.ldu=t.rdu=(int)(next()%8193)-4096;
            t.ldv=t.rdv=(int)(next()%8193)-4096;
        }
        /* Exact stable gradients with moving/slanted edges, clipping and wrap.
         * Other cases retain mismatched slopes and short/empty-row fallbacks. */
        if(i%4==0) {
            t.rdx=t.ldx;t.rdu=t.ldu;t.rdv=t.ldv;
        }
        memset(a,197,sizeof(a)); memcpy(b,a,sizeof(a));
        reference_raster_set_target(a+16,maxX,80,512,scale);
        raster_set_target(b+16,maxX,80,512,scale);
        r=t; r.row=b+16; old[k](&t); current[k](&r);
        if(memcmp(a,b,sizeof(a)) || t.row-(a+16)!=r.row-(b+16)) {
            fprintf(stderr,"mismatch trap=%d case=%d\n",k,i); return 2;
        }
        t.row=r.row=0;
        if(memcmp(&t,&r,sizeof(t))) return 3;
        for(j=0;j<16;++j) if(a[j]!=197 || a[sizeof(a)-16+j]!=197) return 4;
    }
    free(allocation); puts("PASS: 16000 trapezoids, exact pixels, guards and mutated Trap state"); return 0;
}
