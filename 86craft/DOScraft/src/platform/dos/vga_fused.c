#include <limits.h>
#include <stddef.h>
#include "vga_pack.h"
#include "vga_fused.h"

#if (defined(__GNUC__) || defined(__clang__)) && UINT_MAX == 0xffffffffu && \
    CHAR_BIT == 8 && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && \
    !defined(DOS_VGA_FUSED_PORTABLE)
typedef unsigned int FusedWord __attribute__((__may_alias__, __aligned__(1)));
static unsigned int load4(const unsigned char *p) { return *(const FusedWord *)p; }
#else
static unsigned int load4(const unsigned char *p)
{
    return (unsigned int)p[0]|((unsigned int)p[1]<<8)|
           ((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);
}
#endif

int dos_vga_pack_fused(const unsigned char *source,unsigned int pitch,
                       unsigned char *planes,unsigned int capacity,
                       struct DosVgaRun *runs,unsigned int run_capacity)
{
    unsigned char masks[4][200];
    unsigned int y,x,plane,count=0;
    if(!source || !planes || !runs || pitch<320 ||
       capacity<4u*DOS_VGA_PLANE_BYTES || run_capacity<800 ||
       (size_t)239>((size_t)-1-320u)/pitch) return -1;
    for(y=0;y<200;++y) {
        const unsigned char *row=source+(size_t)y*pitch,*s=row;
        unsigned char *p=planes+y*80;
        unsigned int pairs=0,cross=0;
        /* Read each source word once; only owners 0/2 need packing if pairs
         * are equal. Accumulate exact equality while those bytes are live. */
#define GROUP(i) do { \
            unsigned int v=load4(s+4u*(i)); \
            p[i]=(unsigned char)v; \
            p[2u*DOS_VGA_PLANE_BYTES+(i)]=(unsigned char)(v>>16); \
            pairs|=v^(v>>8); cross|=v^(v>>16); \
        } while(0)
        for(x=0;x<80;x+=4) {
            GROUP(0); GROUP(1); GROUP(2); GROUP(3);
            s+=16;p+=4;
        }
#undef GROUP
        /* Sharp overlays/arbitrary images are row-local exact fallbacks,
         * not a global trust of the doubled-resolution hint. */
        if(pairs&0xffu) {
            p=planes+DOS_VGA_PLANE_BYTES+y*80;
            for(x=0;x<80;++x) p[x]=row[4*x+1];
        }
        if(pairs&0xff0000u) {
            p=planes+3u*DOS_VGA_PLANE_BYTES+y*80;
            for(x=0;x<80;++x) p[x]=row[4*x+3];
        }
        if(!(pairs&0xff00ffu) && !(cross&0xffu)) {
            masks[0][y]=15;masks[1][y]=masks[2][y]=masks[3][y]=0;
        } else {
            masks[0][y]=(pairs&0xffu)?1:3;
            masks[1][y]=(pairs&0xffu)?2:0;
            masks[2][y]=(pairs&0xff0000u)?4:12;
            masks[3][y]=(pairs&0xff0000u)?8:0;
        }
    }
    /* HUD is sharp and page-keyed: always supply all four complete planes. */
    for(y=200;y<240;++y) {
        const unsigned char *s=source+(size_t)y*pitch;
        unsigned char *p=planes+y*80;
#define HUD(i) do { \
            unsigned int v=load4(s+4u*(i)); \
            p[i]=(unsigned char)v; \
            p[DOS_VGA_PLANE_BYTES+(i)]=(unsigned char)(v>>8); \
            p[2u*DOS_VGA_PLANE_BYTES+(i)]=(unsigned char)(v>>16); \
            p[3u*DOS_VGA_PLANE_BYTES+(i)]=(unsigned char)(v>>24); \
        } while(0)
        for(x=0;x<80;x+=4) { HUD(0); HUD(1); HUD(2); HUD(3);s+=16;p+=4; }
#undef HUD
    }
    for(plane=0;plane<4;++plane) {
        y=0;
        while(y<200) {
            unsigned int start=y;
            unsigned char mask=masks[plane][y++];
            if(!mask) continue;
            while(y<200 && masks[plane][y]==mask) ++y;
            runs[count].row=(unsigned short)start;
            runs[count].rows=(unsigned short)(y-start);
            runs[count].mask=mask;
            runs[count].plane=(unsigned char)plane;
            ++count;
        }
    }
    return (int)count;
}
