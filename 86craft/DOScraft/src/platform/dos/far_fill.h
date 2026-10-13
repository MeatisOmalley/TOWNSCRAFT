#ifndef DOS_FAR_FILL_H
#define DOS_FAR_FILL_H
#include "common.h"
typedef struct {
    u8 *cursor;
    int top,stride,mode,scale,x;
} FarColumn;
void raster_far_column(FarColumn *c,int x,int width,int top);

enum { FAR_PATTERN_CHECKER,FAR_PATTERN_SOLID,FAR_PATTERN_LEAVES,
       FAR_PATTERN_LOG,FAR_PATTERN_PLANK,FAR_PATTERN_GRAIN };

/* Material patterns are sparse, one shade apart, and retain paired rows.
 * Construct packed words outside the fill loop; no per-pixel texture reads. */
static inline void far_fill_pattern(FarColumn *c,int y0,u8 a,u8 b,int phase,int pattern)
{
    int y=c->top,offset=(c->x+phase*2)&3;
    u8 v[4]={a,a,a,a};
    unsigned rows;
    if(pattern==FAR_PATTERN_PLANK) {
        for(int i=0;i<4;++i) v[i]=b;
        rows=phase ? 0x40 : 0x04;
    } else {
        if(pattern!=FAR_PATTERN_LOG || !((c->x+phase*2)&4)) v[(3-offset)&3]=b;
        rows=pattern==FAR_PATTERN_LEAVES ? (phase ? 0x88 : 0x22) :
             pattern==FAR_PATTERN_LOG ? 0x7e : (phase ? 0x10 : 0x01);
    }
    u32 solid=(u32)a*0x01010101u,word,second;
    if(c->scale==2) {
        word=(u32)v[0]*0x0101u|((u32)v[1]*0x0101u<<16);
        second=(u32)v[2]*0x0101u|((u32)v[3]*0x0101u<<16);
    } else {
        word=v[0]|((u32)v[1]<<8)|((u32)v[2]<<16)|((u32)v[3]<<24);
        second=pattern==FAR_PATTERN_LOG ? solid : word;
    }
    u16 left=v[0]|((u16)v[2]<<8),right=v[1]|((u16)v[3]<<8);
    u32 paint[2]={solid,word},next[2]={solid,second};
    u16 even[2]={(u16)solid,left},odd[2]={(u16)solid,right};
    u8 *p=c->cursor;
    c->cursor-=(y-y0)*c->stride; c->top=y0;
#define FAR_ROWS for(;y>y0;) { --y; p-=c->stride; int k=(rows>>((y>>1)&7))&1;
#define FAR_END }
    switch(c->mode) {
    case 1: FAR_ROWS *p=paint[k]; FAR_END break;
    case 2: FAR_ROWS *(u16 *)p=paint[k]; FAR_END break;
    case 4: FAR_ROWS *(u32 *)p=paint[k]; FAR_END break;
    case 8: FAR_ROWS *(u32 *)p=paint[k]; *(u32 *)(p+4)=next[k]; FAR_END break;
    case 9: FAR_ROWS *p=even[k]; FAR_END break;
    case 10: FAR_ROWS *p=even[k]; p[38400]=odd[k]; FAR_END break;
    case 12: FAR_ROWS *(u16 *)p=even[k]; *(u16 *)(p+38400)=odd[k]; FAR_END break;
    }
#undef FAR_ROWS
#undef FAR_END
}

/* Each live ray only grows upward. Dispatch the target layout/width once
 * per strip, and keep the bottom cursor between successive march samples. */
static inline void far_fill_column(FarColumn *c,int y0,u8 a,u8 b,int phase)
{
    if(y0>=c->top || c->mode<0) return;
    int pattern=phase>>1; phase&=1;
    if(pattern==FAR_PATTERN_SOLID) b=a;
    if(pattern>=FAR_PATTERN_LEAVES && a!=b) {
        far_fill_pattern(c,y0,a,b,phase,pattern); return;
    }
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
