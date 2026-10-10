/* Real assembly painter is the image oracle. Dual-background primitive draws
 * independently reveal actual opaque coverage, including opaque color zero.
 * This slow oracle is for correctness only, not a proposed shipping renderer.
 */
#include "f2b_capture.h"
#include "coverage_spans.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BYTES (512*240+32)
static unsigned char painter[BYTES],forward[BYTES],reverse[BYTES],a[BYTES],b[BYTES];
static struct DosF2BCapture capture;
static struct DosCoverageSpans coverage;
static int width,height,scale,emissions;
static unsigned int rng=19973;
static unsigned int next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }

static void draw(const struct DosF2BCommand *cmd,void *unused)
{
    (void)unused;
    if(cmd->kind==DOS_F2B_TEXTURED) raster_poly(cmd->vertices,cmd->count,cmd->tile,cmd->flags);
    else if(cmd->kind==DOS_F2B_FLAT) raster_flat_poly(cmd->vertices,cmd->count,(u8)cmd->color);
    else raster_pixel(cmd->vertices[0].x,cmd->vertices[0].y,(u8)cmd->color);
}

static void first_opaque(const struct DosF2BCommand *cmd,void *unused)
{
    int x,y;
    (void)unused;
    memset(a,0,sizeof(a)); memset(b,255,sizeof(b));
    raster_set_target(a+16,width,height,512,scale); draw(cmd,NULL);
    raster_set_target(b+16,width,height,512,scale); draw(cmd,NULL);
    for(y=0;y<height;++y) for(x=0;x<width;++x) {
        int i=16+y*scale*512+x*scale;
        if(a[i]==b[i] && dos_coverage_mark_pixel(&coverage,x,y)==1) {
            reverse[i]=a[i];
            if(scale==2) reverse[i+1]=a[i];
        }
    }
}

static void count_emit(const struct DosF2BCommand *cmd,void *unused)
{ (void)cmd; (void)unused; ++emissions; }

static void boundaries(const u8 *tile)
{
    static struct { unsigned int before[4]; struct DosF2BCapture c; unsigned int after[4]; } guarded;
    static struct DosF2BCapture snapshot;
    RVert vertices[12]; int i;
    for(i=0;i<4;++i) guarded.before[i]=guarded.after[i]=0x812973bu;
    for(i=0;i<12;++i) vertices[i]=(RVert){i*16,i*16,i*65536,i*65536};
    dos_f2b_capture_reset(&guarded.c);
    assert(dos_f2b_capture_poly(&guarded.c,vertices,12,tile,3)==1);
    assert(!memcmp(guarded.c.commands[0].vertices,vertices,sizeof(vertices)));
    vertices[0].x=9999;
    assert(guarded.c.commands[0].vertices[0].x==0);
    snapshot=guarded.c;
    assert(dos_f2b_capture_poly(&guarded.c,vertices,13,tile,0)==-1);
    assert(dos_f2b_capture_poly(&guarded.c,vertices,4,NULL,0)==-1);
    assert(dos_f2b_capture_poly(&guarded.c,vertices,4,tile,4)==-1);
    assert(dos_f2b_capture_flat(&guarded.c,NULL,4,0)==-1);
    assert(!memcmp(&snapshot,&guarded.c,sizeof(snapshot)));
    emissions=0;
    assert(dos_f2b_capture_replay(&guarded.c,2,count_emit,NULL)==-1);
    assert(dos_f2b_capture_replay(&guarded.c,0,NULL,NULL)==-1);
    assert(!emissions);
    for(i=1;i<DOS_F2B_COMMANDS;++i) assert(dos_f2b_capture_point(&guarded.c,i,0,0)==1);
    assert(dos_f2b_capture_point(&guarded.c,0,0,0)==0);
    assert(dos_f2b_capture_replay(&guarded.c,0,count_emit,NULL)==0);
    assert(dos_f2b_capture_replay(&guarded.c,1,count_emit,NULL)==0);
    assert(!emissions);
    for(i=0;i<4;++i) assert(guarded.before[i]==0x812973bu && guarded.after[i]==0x812973bu);
    dos_f2b_capture_reset(&guarded.c);
    assert(dos_f2b_capture_point(&guarded.c,1,2,0)==1);
    assert(dos_f2b_capture_point(&guarded.c,3,4,255)==1);
    guarded.c.commands[1].count=2;
    assert(dos_f2b_capture_replay(&guarded.c,0,count_emit,NULL)==-1);
    assert(!emissions); /* No partially emitted valid prefix on corruption. */
    dos_f2b_capture_reset(NULL);
    assert(dos_f2b_capture_point(NULL,0,0,0)==-1);
}

int main(void)
{
    unsigned char *allocation=malloc(196608),*page,*tile;
    int scene,i,j;
    assert(allocation);
    page=(unsigned char *)(((uintptr_t)allocation+65535)&~(uintptr_t)65535);
    tile=page+0x2010;
    for(i=0;i<65536;++i) page[i]=(i%5==0)?0:(unsigned char)(i*71+13);
    boundaries(tile);
    for(scale=1;scale<=2;++scale) for(scene=0;scene<24;++scene) {
        width=320/scale; height=200/scale;
        memset(painter,197,sizeof(painter)); memset(forward,197,sizeof(forward));
        memset(reverse,197,sizeof(reverse));
        dos_f2b_capture_reset(&capture);
        raster_set_target(painter+16,width,height,512,scale);
        for(i=0;i<64;++i) {
            int flags=i%4,x=(int)(next()%(width+64))-32,y=(int)(next()%(height+64))-32;
            int w=1+(int)(next()%(width/2)),h=1+(int)(next()%(height/2));
            int skew=(int)(next()%17)-8,uv=(flags&RP_WRAP)?(64*65536-1):(16*65536-1);
            RVert v[4]={{x*16,y*16,512,512},{(x+w)*16,y*16,uv,512},
                {(x+w+skew)*16,(y+h)*16,uv,uv},{(x+skew)*16,(y+h)*16,512,uv}};
            for(j=0;j<4;++j) { v[j].x+=next()%8; v[j].y+=next()%8; }
            if(i&1) { RVert swap=v[1]; v[1]=v[3]; v[3]=swap; }
            g_fastFlat=i&1;
            if(i%9==0) {
                raster_pixel(x,y,0);
                assert(dos_f2b_capture_point(&capture,x,y,0)==1);
            } else if(i%7==0) {
                u8 color=(i%14==0)?0:47;
                raster_flat_poly(v,4,color);
                assert(dos_f2b_capture_flat(&capture,v,4,color)==1);
            } else {
                raster_poly(v,4,tile,flags);
                assert(dos_f2b_capture_poly(&capture,v,4,tile,flags)==1);
            }
            memset(v,0,sizeof(v)); /* Captures must own their stack vertices. */
        }
        raster_finish();
        raster_set_target(forward+16,width,height,512,scale);
        assert(dos_f2b_capture_replay(&capture,0,draw,NULL)==1); raster_finish();
        assert(!memcmp(painter,forward,sizeof(painter)));
        assert(dos_coverage_reset(&coverage,width,height)==1);
        assert(dos_f2b_capture_replay(&capture,1,first_opaque,NULL)==1);
        raster_set_target(reverse+16,width,height,512,scale); raster_finish();
        assert(!memcmp(painter,reverse,sizeof(painter)));
    }
    free(allocation);
    puts("PASS: 48 actual-assembly scenes, forward capture and reverse first-opaque identity, bounds/overflow/vertex ownership");
    return 0;
}
