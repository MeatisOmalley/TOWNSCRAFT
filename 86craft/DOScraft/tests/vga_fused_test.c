#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "vga_pack.h"
#include "vga_fused.h"
#define GUARD 32
static unsigned char src[512*240+64],original[sizeof(src)];
static unsigned char packed[76800+64],vram[4][19200];
static struct {
    unsigned char before[32];
    struct DosVgaRun runs[804];
    unsigned char after[32];
} output,untouched;
static uint32_t state=0x48625abc;
static unsigned int random_value(void) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
static void fill(unsigned int pitch,unsigned int offset,unsigned int pattern) {
    unsigned int y,x;
    memset(src,0xa9,sizeof(src));
    for(y=0;y<240;++y) for(x=0;x<320;x+=4) {
        unsigned char *p=src+GUARD+offset+y*pitch+x;
        unsigned int kind=y>=200?5:(pattern==6?y%6:pattern);
        p[0]=(unsigned char)random_value();p[1]=(unsigned char)random_value();
        p[2]=(unsigned char)random_value();p[3]=(unsigned char)random_value();
        if(kind==0) p[0]=p[1]=p[2]=p[3]=(unsigned char)y;
        if(kind==1) {p[1]=p[0];p[3]=p[2];}
        if(kind==2) p[1]=p[0];
        if(kind==3) p[3]=p[2];
        if(kind==4) p[0]=p[1]=p[2]=p[3]=(unsigned char)(x/4+y);
    }
}
static void check(unsigned int pitch,unsigned int offset) {
    unsigned int i,p,y,x,mask[4][200],next[4]={0,0,0,0};
    unsigned char *s=src+GUARD+offset,*d=packed+GUARD+offset;
    int count;
    memset(packed,0xcd,sizeof(packed));memset(vram,0x6a,sizeof(vram));
    memset(&output,0xcd,sizeof(output));memcpy(&untouched,&output,sizeof(output));
    memcpy(original,src,sizeof(src));
    count=dos_vga_pack_fused(s,pitch,d,76800,output.runs,800);
    assert(count>0 && count<=800);
    /* Byte-only input equality oracle, independent of packing/word order. */
    for(y=0;y<200;++y) {
        int a=1,b=1,c=1;
        for(x=0;x<320;x+=4) {
            if(s[y*pitch+x]!=s[y*pitch+x+1]) a=0;
            if(s[y*pitch+x+2]!=s[y*pitch+x+3]) b=0;
            if(s[y*pitch+x]!=s[y*pitch+x+2]) c=0;
        }
        mask[0][y]=a&&b&&c?15:(a?3:1);
        mask[1][y]=a?0:2;
        mask[2][y]=a&&b&&c?0:(b?12:4);
        mask[3][y]=b?0:8;
    }
    for(i=0;i<(unsigned int)count;++i) {
        const struct DosVgaRun *r=output.runs+i;
        assert(r->plane<4 && r->rows && r->row+r->rows<=200);
        if(i) {
            const struct DosVgaRun *old=output.runs+i-1;
            assert(old->plane<=r->plane);
            if(old->plane==r->plane) {
                assert(old->row+old->rows<=r->row);
                assert(old->mask!=r->mask || old->row+old->rows!=r->row);
            }
        }
        while(next[r->plane]<r->row) assert(!mask[r->plane][next[r->plane]++]);
        assert(next[r->plane]==r->row);
        for(y=r->row;y<r->row+r->rows;++y) {
            assert(mask[r->plane][y]==r->mask);
            for(p=0;p<4;++p) if(r->mask&(1u<<p))
                memcpy(vram[p]+y*80,d+r->plane*19200+y*80,80);
        }
        next[r->plane]=r->row+r->rows;
    }
    for(p=0;p<4;++p) {
        while(next[p]<200) assert(!mask[p][next[p]++]);
        memcpy(vram[p]+16000,d+p*19200+16000,3200);
        for(y=0;y<240;++y) for(x=0;x<80;++x)
            assert(vram[p][y*80+x]==s[y*pitch+4*x+p]);
    }
    assert(!memcmp(src,original,sizeof(src)));
    for(i=0;i<GUARD+offset;++i) assert(packed[i]==0xcd);
    for(i=GUARD+offset+76800;i<sizeof(packed);++i) assert(packed[i]==0xcd);
    assert(!memcmp(output.before,untouched.before,sizeof(output.before)));
    assert(!memcmp(output.after,untouched.after,sizeof(output.after)));
    assert(!memcmp(output.runs+count,untouched.runs+count,
                   sizeof(output.runs)-count*sizeof(output.runs[0])));
}
static void protected_bounds(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    unsigned int page,y,x,p,i;
    unsigned char *input,*destination,*run_store,*s,*d;
    struct DosVgaRun *runs;
    DWORD old;
    int count;
    GetSystemInfo(&info);page=info.dwPageSize;assert(page>=320);
    input=VirtualAlloc(NULL,480u*page,MEM_RESERVE,PAGE_NOACCESS);
    destination=VirtualAlloc(NULL,20u*page,MEM_RESERVE,PAGE_NOACCESS);
    run_store=VirtualAlloc(NULL,3u*page,MEM_RESERVE,PAGE_NOACCESS);
    assert(input && destination && run_store && 19u*page>=76800 && 2u*page>=800*sizeof(*runs));
    s=input+page-320;
    for(y=0;y<240;++y) {
        assert(VirtualAlloc(input+2u*y*page,page,MEM_COMMIT,PAGE_READWRITE));
        for(x=0;x<320;++x) s[2u*y*page+x]=(unsigned char)(x/2+y);
        s[2u*y*page+319]^=1; /* Last source word includes a sharp exception. */
        assert(VirtualProtect(input+2u*y*page,page,PAGE_READONLY,&old));
    }
    assert(VirtualAlloc(destination,19u*page,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(run_store,2u*page,MEM_COMMIT,PAGE_READWRITE));
    d=destination+19u*page-76800;
    runs=(struct DosVgaRun *)(void *)(run_store+2u*page-800*sizeof(*runs));
    /* Invalid calls must not even inspect inaccessible inputs/outputs. */
    assert(dos_vga_pack_fused(input+page,2u*page,d,76799,runs,800)==-1);
    assert(dos_vga_pack_fused(s,2u*page,destination+19u*page,76800,runs,799)==-1);
    count=dos_vga_pack_fused(s,2u*page,d,76800,runs,800);
    assert(count>0 && count<=800);
    memset(vram,0,sizeof(vram));
    for(i=0;i<(unsigned int)count;++i) {
        const struct DosVgaRun *r=runs+i;
        assert(r->plane<4 && r->rows && r->row+r->rows<=200);
        for(p=0;p<4;++p) if(r->mask&(1u<<p))
            memcpy(vram[p]+r->row*80,d+r->plane*19200+r->row*80,r->rows*80);
    }
    for(p=0;p<4;++p) {
        memcpy(vram[p]+16000,d+p*19200+16000,3200);
        for(y=0;y<240;++y) for(x=0;x<80;++x)
            assert(vram[p][y*80+x]==s[2u*y*page+4*x+p]);
    }
    assert(VirtualFree(input,0,MEM_RELEASE));
    assert(VirtualFree(destination,0,MEM_RELEASE));
    assert(VirtualFree(run_store,0,MEM_RELEASE));
#endif
    puts("PASS: protected row ends/source immutability/destination/run bounds");
}
int main(int argc,char **argv) {
    unsigned int p,o,k,x,bit;
    const unsigned int pitches[]={320,321,331,512};
    assert(argc==2);
    if(!strcmp(argv[1],"matrix")) {
        for(p=0;p<4;++p) for(o=0;o<16;++o) for(k=0;k<7;++k) {
            fill(pitches[p],o,k);check(pitches[p],o);
        }
        puts("PASS: 448 pitched/unaligned/random/paired/sharp/HUD images");
    } else if(!strcmp(argv[1],"bits")) {
        for(x=0;x<320;++x) for(bit=0;bit<8;++bit) {
            unsigned int row=(x*73+bit*31)%200;
            fill(331,1,1);src[GUARD+1+row*331+x]^=(unsigned char)(1u<<bit);
            check(331,1);
        }
        puts("PASS: 2560 individual sharp pixel/bit exceptions");
    } else if(!strcmp(argv[1],"protected")) {
        protected_bounds();
    } else {
        unsigned char before[sizeof(packed)];
        fill(320,0,0);memset(packed,0xcd,sizeof(packed));memcpy(before,packed,sizeof(packed));
        memset(&output,0xcd,sizeof(output));memcpy(&untouched,&output,sizeof(output));
        for(k=0;k<5;++k) {
            int result=dos_vga_pack_fused(k==0?NULL:src,k==3?319:320,
                k==1?NULL:packed,k==4?76799:76800,k==2?NULL:output.runs,800);
            assert(result==-1);
        }
        assert(dos_vga_pack_fused(src,320,packed,76800,output.runs,799)==-1);
        assert(!memcmp(before,packed,sizeof(packed)) && !memcmp(&output,&untouched,sizeof(output)));
        puts("PASS: invalid arguments/capacities cause no writes");
    }
    return 0;
}
