/* Host i386 parity of production and candidate trapezoid assembly. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned int u32;
typedef struct {
    u8 *row; int stride,rows,lx,ldx,lu,ldu,lv,ldv;
    int rx,rdx,ru,rdu,rv,rdv; const u8 *tile; int maxX; u32 pixels; int rowIndex;
} Trap;
int g_recip14[321];
u8 dos_very_low_rows[100];
#define D(n) void n(Trap *); void reference_##n(Trap *);
D(trap_opaque1) D(trap_opaque2) D(trap_transp1) D(trap_transp2)
D(trap_opaque1w) D(trap_opaque2w) D(trap_transp1w) D(trap_transp2w)
D(trap_opaque2v) D(trap_transp2v) D(trap_opaque2wv) D(trap_transp2wv)
D(trap_opaqueP) D(trap_transpP) D(trap_opaquePw) D(trap_transpPw)
#define P(n) {reference_##n,n,#n}
static struct { void (*ref)(Trap *),(*cand)(Trap *); const char *name; } fn[]={
    P(trap_opaque1),P(trap_opaque2),P(trap_transp1),P(trap_transp2),
    P(trap_opaque1w),P(trap_opaque2w),P(trap_transp1w),P(trap_transp2w),
    P(trap_opaque2v),P(trap_transp2v),P(trap_opaque2wv),P(trap_transp2wv),
    P(trap_opaqueP),P(trap_transpP),P(trap_opaquePw),P(trap_transpPw)};
static u32 seed=0x5eed1234u;
static u32 next(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
static int rnd(int lo,int hi) { return lo+(int)(next()%(u32)(hi-lo+1)); }
#define BUF (38400+160*110+64)
int main(int argc,char **argv)
{
    static u8 a[BUF],b[BUF];
    int cases=argc>1?atoi(argv[1]):20000,k,i,j;
    u8 *allocation=malloc(4<<20),*page;
    long total=0;
    if(!allocation || sizeof(Trap)!=76) return 1;
    page=(u8 *)(((uintptr_t)allocation+65535)&~(uintptr_t)65535);
    for(i=0;i<(3<<20);++i) page[i]=(i%7)?(u8)(i*43+17):0;
    for(i=1;i<=320;++i) g_recip14[i]=16384/i;
    for(k=0;k<16;++k) for(i=0;i<cases;++i) {
        int planar=k>=12,vl=k>=8&&k<12,scale=planar?1:(vl?2:1+(k&1));
        int maxX=planar?160:320/scale,stride=planar?160:512;
        int rows=rnd(1,i%3?8:60),x=rnd(-80,maxX+20),width=rnd(-4,i%5?24:maxX+40);
        Trap t,r;
        memset(&t,0,sizeof(t));
        t.row=a+16; t.stride=stride; t.rows=i==0?0:rows;
        t.lx=x*65536+rnd(-32768,32767); t.rx=t.lx+width*65536+rnd(-32768,32767);
        if(i&8) { t.ldx=rnd(-3*65536,3*65536); t.rdx=rnd(-3*65536,3*65536); }
        t.lu=(int)next(); t.lv=(int)next();
        if(i&1) { t.lu&=0xFFFFF; t.lv&=0xFFFFF; }
        t.ru=t.lu+rnd(-40*65536,40*65536); t.rv=t.lv+rnd(-40*65536,40*65536);
        if(i%3==0) t.ru=t.lu+rnd(-300,300);
        if(i%4==0) t.rv=t.lv+rnd(-300,300);
        if(i&16) { t.ldu=rnd(-70000,70000); t.rdu=rnd(-70000,70000); t.ldv=rnd(-70000,70000); t.rdv=rnd(-70000,70000); }
        t.tile=page+(next()%768)*4096+(next()%16)*16;
        t.maxX=maxX; t.pixels=next()%5;
        t.rowIndex=rnd(0,100-rows>0?100-rows:0);
        for(j=0;j<100;++j) dos_very_low_rows[j]=next()&1;
        memset(a,197,sizeof(a)); memcpy(b,a,sizeof(a));
        r=t; r.row=b+16;
        fn[k].ref(&t); fn[k].cand(&r);
        if(memcmp(a,b,sizeof(a)) || t.row-a!=r.row-b) {
            fprintf(stderr,"pixel mismatch %s case %d\n",fn[k].name,i); return 2;
        }
        t.row=r.row=0;
        if(memcmp(&t,&r,sizeof(t))) { fprintf(stderr,"state mismatch %s case %d\n",fn[k].name,i); return 3; }
        total+=t.pixels;
    }
    printf("PASS: %d trapezoids x 16 entry points, exact pixels and Trap state (%ld pixels)\n",cases,total);
    free(allocation); return 0;
}
