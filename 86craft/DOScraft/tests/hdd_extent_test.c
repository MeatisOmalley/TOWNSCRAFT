#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hdd_extent.h"
static unsigned char disk[4132][512],header[512];
static int fail_sector=-1;
static int read_sector(unsigned sector,unsigned count,unsigned char *dst)
{
    assert(count==1 && sector<4132);
    if((int)sector==fail_sector) return 0;
    memcpy(dst,disk[sector],512); return 1;
}
static void word(unsigned char *p,unsigned n) { p[0]=n; p[1]=n>>8; }
static void dword(unsigned char *p,unsigned n) { word(p,n); word(p+2,n>>16); }
int main(void)
{
    unsigned first=999;
    unsigned char appendix[512]={0};memcpy(appendix,"DOSCRAFT-1024-V1",16);
    assert(dos_terrain_known_medium(178815488,appendix));
    assert(!dos_terrain_known_medium(179024384,appendix));
    dword(appendix+52,0x314c4354u);dword(appendix+72,0x31465254u);
    dword(appendix+76,178291200u/512);dword(appendix+80,524288);dword(appendix+88,1);
    dword(appendix+92,0x31525446u);dword(appendix+96,178815488u/512);
    dword(appendix+100,208896);dword(appendix+108,37273);dword(appendix+112,6533);
    assert(dos_terrain_known_medium(179024384,appendix));
    assert(!dos_terrain_known_medium(179024896,appendix));
    dword(appendix+108,~0u);assert(!dos_terrain_known_medium(179024384,appendix));
    dword(appendix+108,37273);appendix[92]^=1;assert(!dos_terrain_known_medium(179024384,appendix));
    assert(!dos_terrain_edit_range(0,1) && !dos_terrain_edit_range(33,24));
    assert(dos_terrain_edit_range(57,24) && dos_terrain_edit_range(81,24));
    assert(!dos_terrain_edit_range(80,2) && !dos_terrain_edit_range(104,2));
    assert(!dos_terrain_edit_range(57,25) && !dos_terrain_edit_range(57,0));
    assert(dos_terrain_edit_range(294921,24));
    assert(!dos_terrain_edit_range(294944,2) && !dos_terrain_edit_range(294945,1));
    assert(!dos_terrain_edit_range(294977,13) && !dos_terrain_edit_range(~0u,24));
    /* V3: any record of up to 24 sectors inside the edit log, nothing else. */
    unsigned char log[512]={0},v2[512]={0};
    const unsigned at=47652,end=47652+32768;
    memcpy(log,"DOSCRAFT-1024-V3",16); dword(log+16,3);
    dword(log+132,at); dword(log+136,32768); dword(log+140,end);
    assert(!dos_terrain_known_medium(end*512u+512,log) && dos_terrain_known_medium(end*512u,log));
    assert(dos_terrain_edit_range(at,24) && dos_terrain_edit_range(at+1,7) &&
           dos_terrain_edit_range(end-1,1) && dos_terrain_edit_range(end-24,24));
    assert(!dos_terrain_edit_range(at-1,1) && !dos_terrain_edit_range(at-1,2) &&
           !dos_terrain_edit_range(end,1) && !dos_terrain_edit_range(end-1,2) &&
           !dos_terrain_edit_range(at,25) && !dos_terrain_edit_range(at,0) &&
           !dos_terrain_edit_range(0,1) && !dos_terrain_edit_range(~0u,1));
    dword(log+16,2); assert(!dos_terrain_known_medium(end*512u,log)); dword(log+16,3);
    dword(log+136,2047); dword(log+140,at+2047); assert(!dos_terrain_known_medium((at+2047)*512u,log));
    dword(log+136,65537); dword(log+140,at+65537); assert(!dos_terrain_known_medium((at+65537)*512u,log));
    dword(log+136,32768); dword(log+140,end+1); assert(!dos_terrain_known_medium((end+1)*512u,log));
    /* A V2 header with the same words keeps its slot-aligned guard. */
    memcpy(v2,"DOSCRAFT-1024-V2",16); dword(v2+16,2);
    dword(v2+132,at); dword(v2+136,384); dword(v2+140,at+384*48);
    assert(dos_terrain_known_medium((at+384*48)*512u,v2));
    assert(dos_terrain_edit_range(at,24) && !dos_terrain_edit_range(at+1,24) && !dos_terrain_edit_range(at+23,2));
    unsigned char *boot=disk[0],*entry=disk[35],*fat=disk[1];
    word(boot+11,512); boot[13]=1; word(boot+14,1); boot[16]=2;
    word(boot+17,16); word(boot+19,4132); word(boot+22,17);
    boot[510]=0x55; boot[511]=0xaa;
    memcpy(entry,"TERRAIN TMP",11); word(entry+26,2); dword(entry+28,1024);
    word(fat+4,3); word(fat+6,0xffff);
    memset(header,0x27,sizeof(header)); memcpy(disk[36],header,512);
    assert(dos_terrain_find_extent(read_sector,1024,header,&first) && first==36);
    first=999;
    word(fat+4,5); assert(!dos_terrain_find_extent(read_sector,1024,header,&first) && first==999);
    word(fat+4,2); assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    word(fat+4,0xffff); assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    word(fat+4,3); word(fat+6,4); assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    word(fat+6,0xffff); dword(entry+28,512); assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    dword(entry+28,1024); word(entry+26,4097); assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    word(entry+26,2); entry[11]=16; assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    entry[11]=0; disk[36][0]^=1; assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    disk[36][0]^=1;
    for(int i=0;i<4;++i) {
        fail_sector=i==0?0:i==1?35:i==2?1:36;
        assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    }
    fail_sector=-1; boot[13]=3; assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    boot[13]=1; word(boot+22,1); assert(!dos_terrain_find_extent(read_sector,1024,header,&first));
    word(boot+22,17); assert(dos_terrain_find_extent(read_sector,1024,header,&first));
    puts("PASS: bounded FAT16 extent / fragmentation / corrupt chain / identity / I/O faults");
    return 0;
}
