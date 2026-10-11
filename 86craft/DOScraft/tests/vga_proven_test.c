#include <stdio.h>
#include <string.h>
#include "vga_proven.h"
#include "vga_pack.h"

static unsigned int seed=0x73cd9a21u;
static unsigned char next_byte(void)
{
    seed=seed*1664525u+1013904223u;
    return (unsigned char)(seed>>16);
}

int main(void)
{
    unsigned char source[240*337+8], packed[4*DOS_VGA_PLANE_BYTES+8];
    unsigned char vga[4*DOS_VGA_PLANE_BYTES];
    unsigned char proof[200];
    struct DosVgaRun runs[DOS_VGA_UPLOAD_MAX_RUNS];
    unsigned int case_no,y,x,p,r;
    for (case_no=0; case_no<128; ++case_no) {
        const unsigned char *src=source+(case_no&7u);
        unsigned char *dst=packed+(case_no&7u);
        int count;
        memset(source,0xa5,sizeof(source));
        memset(packed,0x5a,sizeof(packed));
        memset(vga,0x33,sizeof(vga));
        for (y=0; y<200; ++y) proof[y]=(unsigned char)((case_no+y*7u)%5u!=0);
        for (y=0; y<240; ++y) {
            unsigned char *row=source+(case_no&7u)+y*337;
            if (y<200 && proof[y]) {
                for (x=0; x<160; ++x) {
                    unsigned char a=next_byte();
                    row[2*x]=row[2*x+1]=a;
                }
            } else {
                for (x=0; x<320; ++x) row[x]=next_byte();
            }
            if ((case_no&7u)==0 && y<200 && proof[y] && y%13u==0)
                memset(row,17,320);
        }
        count=dos_vga_proven_pack_plan(src,337,dst,4*DOS_VGA_PLANE_BYTES,
                                        proof,runs,DOS_VGA_UPLOAD_MAX_RUNS);
        if (count<0 || (unsigned)count>DOS_VGA_UPLOAD_MAX_RUNS) return 1;
        for (r=0; r<(unsigned)count; ++r) {
            struct DosVgaRun *run=&runs[r];
            if (!run->rows || run->row+run->rows>200 || run->plane>3 ||
                !(run->mask&(1u<<run->plane))) return 2;
            for (p=0; p<4; ++p) if (run->mask&(1u<<p)) {
                memcpy(vga+p*DOS_VGA_PLANE_BYTES+run->row*80,
                       dst+run->plane*DOS_VGA_PLANE_BYTES+run->row*80,
                       run->rows*80);
            }
        }
        for (p=0; p<4; ++p)
            memcpy(vga+p*DOS_VGA_PLANE_BYTES+200*80,
                   dst+p*DOS_VGA_PLANE_BYTES+200*80,40*80);
        for (y=0; y<240; ++y) for (x=0; x<320; ++x) {
            unsigned int index=(x&3u)*DOS_VGA_PLANE_BYTES+y*80+x/4;
            if (vga[index]!=src[y*337+x]) return 3;
        }
        for (x=0; x<(case_no&7u); ++x) if (packed[x]!=0x5a) return 4;
        for (x=(case_no&7u)+4*DOS_VGA_PLANE_BYTES; x<sizeof(packed); ++x)
            if (packed[x]!=0x5a) return 5;
        if (dos_vga_proven_pack_plan(src,337,dst,4*DOS_VGA_PLANE_BYTES-1,
                                     proof,runs,DOS_VGA_UPLOAD_MAX_RUNS)!=-1) return 6;
    }
    puts("vga proven rows PASS");
    return 0;
}
