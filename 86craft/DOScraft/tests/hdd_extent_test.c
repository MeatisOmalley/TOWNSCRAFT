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
    assert(!dos_terrain_edit_range(0,1) && !dos_terrain_edit_range(33,24));
    assert(dos_terrain_edit_range(57,24) && dos_terrain_edit_range(81,24));
    assert(!dos_terrain_edit_range(80,2) && !dos_terrain_edit_range(104,2));
    assert(!dos_terrain_edit_range(57,25) && !dos_terrain_edit_range(57,0));
    assert(dos_terrain_edit_range(294921,24));
    assert(!dos_terrain_edit_range(294944,2) && !dos_terrain_edit_range(294945,1));
    assert(!dos_terrain_edit_range(294977,13) && !dos_terrain_edit_range(~0u,24));
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
