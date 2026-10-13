#ifndef DOS_FAR_FILL_H
#define DOS_FAR_FILL_H
#include "common.h"
typedef struct {
    u8 *cursor;
    int top,stride,mode,scale,x;
} FarColumn;
void raster_far_column(FarColumn *c,int x,int width,int top);

/* Each live ray only grows upward. Dispatch the target layout/width once
 * per strip, and keep the bottom cursor between successive march samples. */
static inline void far_fill_column(FarColumn *c,int y0,u8 a,u8 b,int phase)
{
    if(y0>=c->top || c->mode<0) return;
    int y=c->top,mask=a==b ? 0 : 1;
    u8 *p=c->cursor;
    u32 words[2],planes[2];
    if(c->scale==2) {
        words[0]=(u32)a*0x0101u|((u32)b*0x0101u<<16);
        words[1]=(u32)b*0x0101u|((u32)a*0x0101u<<16);
    } else {
        words[0]=(u32)a*0x00010001u|((u32)b*0x00010001u<<8);
        words[1]=(u32)b*0x00010001u|((u32)a*0x00010001u<<8);
    }
    planes[0]=(u32)a*0x0101u; planes[1]=(u32)b*0x0101u;
    phase+=c->x;
    c->cursor-=(y-y0)*c->stride; c->top=y0;
#define FAR_ROWS for(;y>y0;) { --y; p-=c->stride;
#define FAR_END }
    switch(c->mode) {
    case 1: FAR_ROWS *p=(u8)words[((y>>1)+phase)&mask]; FAR_END break;
    case 2: FAR_ROWS *(u16 *)p=words[((y>>1)+phase)&mask]; FAR_END break;
    case 4: FAR_ROWS *(u32 *)p=words[((y>>1)+phase)&mask]; FAR_END break;
    case 8: FAR_ROWS u32 w=words[((y>>1)+phase)&mask]; *(u32 *)p=w; *(u32 *)(p+4)=w; FAR_END break;
    case 9: FAR_ROWS *p=(u8)planes[((y>>1)+phase)&mask]; FAR_END break;
    case 10: FAR_ROWS int k=((y>>1)+phase)&mask; *p=(u8)planes[k]; p[38400]=(u8)planes[k^mask]; FAR_END break;
    case 12: FAR_ROWS int k=((y>>1)+phase)&mask; *(u16 *)p=planes[k]; *(u16 *)(p+38400)=planes[k^mask]; FAR_END break;
    }
#undef FAR_ROWS
#undef FAR_END
}
#endif
