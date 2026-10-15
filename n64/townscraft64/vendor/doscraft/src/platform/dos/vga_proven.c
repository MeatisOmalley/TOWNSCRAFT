#include <stddef.h>
#include <string.h>
#include <limits.h>
#include "vga_proven.h"
#include "vga_pack.h"

#define VIEW_ROWS 200u
#define ROW_BYTES 80u
#ifdef DOS_VL_PACK_WORDS
#include "vga_pack_words.h"
extern int dos_potato_mode;
#endif
#ifdef DOS_VERY_LOW
extern int dos_very_low_mode;
#endif
#if defined(DOS_VERY_LOW_PLANAR) && (!defined(DOS_VERY_LOW) || !defined(DOS_LOW_PLANAR))
#error Very Low planar output requires Very Low and planar Low
#endif
#if (defined(__GNUC__) || defined(__clang__)) && UINT_MAX == 0xffffffffu && \
    CHAR_BIT == 8 && !defined(DOS_VGA_PROVEN_PORTABLE)
typedef unsigned int ProvenWord __attribute__((__may_alias__, __aligned__(1)));
#define PROVEN_WORD_COMPARE 1
#endif

static int row_equal(const unsigned char *a, const unsigned char *b)
{
    unsigned int x;
#ifdef PROVEN_WORD_COMPARE
    for (x=0; x<ROW_BYTES; x+=4)
        if (*(const ProvenWord *)(a+x)!=*(const ProvenWord *)(b+x)) return 0;
#else
    for (x=0; x<ROW_BYTES; x+=4) {
        if (a[x]!=b[x] || a[x+1]!=b[x+1] || a[x+2]!=b[x+2] || a[x+3]!=b[x+3])
            return 0;
    }
#endif
    return 1;
}

static int pack_plan(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven,
                             struct DosVgaRun *runs, unsigned int run_capacity,
                             int view_only, const unsigned char *planar)
{
    unsigned char masks[4][VIEW_ROWS];
    unsigned int y,x,p,count=0;
    if (!source || !planes || !proven || !runs || pitch<320 ||
        capacity<4u*DOS_VGA_PLANE_BYTES || run_capacity<4u*VIEW_ROWS)
        return -1;
    for (y=0; y<(view_only ? VIEW_ROWS : 240u); ++y) {
        const unsigned char *src=source+(size_t)y*pitch;
        unsigned char *a=planes+y*ROW_BYTES;
        unsigned char *b=a+DOS_VGA_PLANE_BYTES;
        unsigned char *c=b+DOS_VGA_PLANE_BYTES;
        unsigned char *d=c+DOS_VGA_PLANE_BYTES;
#ifdef DOS_LOW_PLANAR
        if (planar && y<VIEW_ROWS && !(y&1u) && planar[y>>1]) {
#ifdef DOS_VERY_LOW_PLANAR
            if (dos_very_low_mode) {
                /* Very Low planar rows: plane-0 bytes only, broadcast. */
                memcpy(a+ROW_BYTES,a,ROW_BYTES);
                masks[0][y]=masks[0][y+1]=15;
                masks[1][y]=masks[1][y+1]=0;
                masks[2][y]=masks[2][y+1]=0;
                masks[3][y]=masks[3][y+1]=0;
                ++y;
                continue;
            }
#endif
            /* The renderer wrote this row of planes 0 and 2 directly. */
            memcpy(a+ROW_BYTES,a,ROW_BYTES);
            memcpy(c+ROW_BYTES,c,ROW_BYTES);
            if (row_equal(a,c)) {
                masks[0][y]=masks[0][y+1]=15;
                masks[2][y]=masks[2][y+1]=0;
            } else {
                masks[0][y]=masks[0][y+1]=3;
                masks[2][y]=masks[2][y+1]=12;
            }
            masks[1][y]=masks[1][y+1]=0;
            masks[3][y]=masks[3][y+1]=0;
            ++y;
            continue;
        }
#else
        (void)planar;
#endif
#ifdef DOS_VERY_LOW
        if (dos_very_low_mode && y<VIEW_ROWS && (proven[y]&1u)) {
#ifdef DOS_VGA_PROVEN_VERTICAL
            if (y+1<VIEW_ROWS && (proven[y+1]&3u)==3u) {
                unsigned char *a2=a+ROW_BYTES;
                for (x=0; x<ROW_BYTES; x+=4) {
#define PACK_VLOW_PAIR(i) do { unsigned char color=src[4*(x+i)]; \
                               a[x+i]=a2[x+i]=color; } while (0)
                    PACK_VLOW_PAIR(0); PACK_VLOW_PAIR(1);
                    PACK_VLOW_PAIR(2); PACK_VLOW_PAIR(3);
#undef PACK_VLOW_PAIR
                }
                masks[0][y]=15;
                masks[0][y+1]=15;
                masks[1][y]=masks[2][y]=masks[3][y]=0;
                masks[1][y+1]=masks[2][y+1]=masks[3][y+1]=0;
                ++y;
            } else
#endif
            {
                for (x=0; x<ROW_BYTES; x+=4) {
#define PACK_VLOW(i) a[x+i]=src[4*(x+i)]
                    PACK_VLOW(0); PACK_VLOW(1); PACK_VLOW(2); PACK_VLOW(3);
#undef PACK_VLOW
                }
                masks[0][y]=15;
                masks[1][y]=masks[2][y]=masks[3][y]=0;
            }
        } else
#endif
#ifdef DOS_VGA_PROVEN_VERTICAL
        if (y+1<VIEW_ROWS && !(y&1u) && (proven[y]&1u) &&
            (proven[y+1]&3u)==3u) {
            unsigned char *a2=a+ROW_BYTES;
            unsigned char *c2=c+ROW_BYTES;
            for (x=0; x<ROW_BYTES; x+=4) {
#define PACK_PAIR(i) do { unsigned char v0=src[4*(x+i)]; \
                             unsigned char v2=src[4*(x+i)+2]; \
                             a[x+i]=a2[x+i]=v0; c[x+i]=c2[x+i]=v2; } while (0)
                PACK_PAIR(0); PACK_PAIR(1); PACK_PAIR(2); PACK_PAIR(3);
#undef PACK_PAIR
            }
            if (row_equal(a,c)) {
                masks[0][y]=masks[0][y+1]=15;
                masks[1][y]=masks[1][y+1]=0;
                masks[2][y]=masks[2][y+1]=0;
                masks[3][y]=masks[3][y+1]=0;
            } else {
                masks[0][y]=masks[0][y+1]=3;
                masks[1][y]=masks[1][y+1]=0;
                masks[2][y]=masks[2][y+1]=12;
                masks[3][y]=masks[3][y+1]=0;
            }
            ++y;
        } else
#endif
#ifdef DOS_VGA_PROVEN_VERTICAL
        if (y<VIEW_ROWS && (proven[y]&1u)) {
#else
        if (y<VIEW_ROWS && proven[y]) {
#endif
            for (x=0; x<ROW_BYTES; x+=4) {
#define PACK_OWNER(i) do { a[x+i]=src[4*(x+i)]; c[x+i]=src[4*(x+i)+2]; } while (0)
                PACK_OWNER(0); PACK_OWNER(1); PACK_OWNER(2); PACK_OWNER(3);
#undef PACK_OWNER
            }
            if (row_equal(a,c)) {
                masks[0][y]=15;
                masks[1][y]=masks[2][y]=masks[3][y]=0;
            } else {
                masks[0][y]=3;
                masks[1][y]=0;
                masks[2][y]=12;
                masks[3][y]=0;
            }
        } else {
            int ab,cd;
#ifdef DOS_VL_PACK_WORDS
            if(dos_potato_mode) dos_vga_pack_row_words(src,a,b,c,d);
            else
#endif
            for (x=0; x<ROW_BYTES; x+=4) {
#define PACK_FULL(i) do { a[x+i]=src[4*(x+i)]; b[x+i]=src[4*(x+i)+1]; \
                           c[x+i]=src[4*(x+i)+2]; d[x+i]=src[4*(x+i)+3]; } while (0)
                PACK_FULL(0); PACK_FULL(1); PACK_FULL(2); PACK_FULL(3);
#undef PACK_FULL
            }
            if (y>=VIEW_ROWS) continue;
            ab=row_equal(a,b); cd=row_equal(c,d);
            if (ab && cd && row_equal(a,c)) {
                masks[0][y]=15;
                masks[1][y]=masks[2][y]=masks[3][y]=0;
            } else {
                masks[0][y]=ab ? 3 : 1;
                masks[1][y]=ab ? 0 : 2;
                masks[2][y]=cd ? 12 : 4;
                masks[3][y]=cd ? 0 : 8;
            }
        }
    }
    for (p=0; p<4; ++p) {
        y=0;
        while (y<VIEW_ROWS) {
            unsigned int first=y;
            unsigned char mask=masks[p][y++];
            if (!mask) continue;
            while (y<VIEW_ROWS && masks[p][y]==mask) ++y;
            runs[count].row=(unsigned short)first;
            runs[count].rows=(unsigned short)(y-first);
            runs[count].mask=mask;
            runs[count].plane=(unsigned char)p;
            ++count;
        }
    }
    return (int)count;
}

int dos_vga_proven_pack_plan(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven,
                             struct DosVgaRun *runs, unsigned int run_capacity)
{
    return pack_plan(source,pitch,planes,capacity,proven,runs,run_capacity,0,0);
}

int dos_vga_proven_pack_plan_view(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven,
                             struct DosVgaRun *runs, unsigned int run_capacity)
{
    return pack_plan(source,pitch,planes,capacity,proven,runs,run_capacity,1,0);
}

#ifdef DOS_LOW_PLANAR
int dos_vga_proven_pack_plan_planar(const unsigned char *source, unsigned int pitch,
                             unsigned char *planes, unsigned int capacity,
                             const unsigned char *proven, const unsigned char *planar,
                             struct DosVgaRun *runs, unsigned int run_capacity,
                             int view_only)
{
    return pack_plan(source,pitch,planes,capacity,proven,runs,run_capacity,view_only,planar);
}
#endif
