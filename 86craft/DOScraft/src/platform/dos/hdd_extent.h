#ifndef DOS_HDD_EXTENT_H
#define DOS_HDD_EXTENT_H
/* Read-only discovery of a contiguous FAT16 TERRAIN.TMP extent. A mismatch
 * leaves the ordinary file transport in charge; no guessed physical offset. */
static unsigned extent_u16(const unsigned char *p)
{ return (unsigned)p[0] | (unsigned)p[1]<<8; }
static unsigned extent_u32(const unsigned char *p)
{ return extent_u16(p) | extent_u16(p+2)<<16; }
static int dos_terrain_edit_range(unsigned lba,unsigned count)
{
    if(lba<33 || lba>=294945u || !count || count>24 || count>294945u-lba) return 0;
    unsigned at=(lba-33)%72;
    return at>=24 && count<=24-(at%24);
}
static int dos_terrain_find_extent(int (*read_sector)(unsigned,unsigned,unsigned char *),
    unsigned file_bytes,const unsigned char *header,unsigned *first_sector)
{
    unsigned char sector[512];
    unsigned spc,reserved,copies,entries,fat,total,root,root_size,data,clusters,start=0;
    unsigned fat_cached=~0u,need,cluster_bytes;
    if(!read_sector(0,1,sector) || sector[510]!=0x55 || sector[511]!=0xaa ||
       extent_u16(sector+11)!=512) return 0;
    spc=sector[13]; reserved=extent_u16(sector+14); copies=sector[16];
    entries=extent_u16(sector+17); fat=extent_u16(sector+22);
    total=extent_u16(sector+19); if(!total) total=extent_u32(sector+32);
    if(!spc || spc>128 || (spc&(spc-1)) || !reserved || copies!=2 ||
       !entries || entries>512 || !fat || fat>256 || total>0x3fffffu) return 0;
    root=reserved+copies*fat; root_size=(entries*32+511)/512;
    data=root+root_size;
    if(total<=data) return 0;
    clusters=(total-data)/spc;
    if(clusters<4085 || clusters>=65525 || (clusters+2)*2>fat*512) return 0;
    for(unsigned s=0;s<root_size && !start;++s) {
        if(!read_sector(root+s,1,sector)) return 0;
        for(unsigned i=0;i<512;i+=32) {
            const unsigned char *e=sector+i;
            if(!e[0]) return 0;
            if(e[0]!=0xe5 && !(e[11]&0x18) && !memcmp(e,"TERRAIN TMP",11)) {
                if(extent_u32(e+28)!=file_bytes) return 0;
                start=extent_u16(e+26); break;
            }
        }
    }
    cluster_bytes=spc*512;
    need=(file_bytes+cluster_bytes-1)/cluster_bytes;
    if(start<2 || !need || start>=clusters+2 || need>clusters+2-start) return 0;
    for(unsigned i=0;i<need;++i) {
        unsigned cluster=start+i,at=cluster*2,next;
        if(at/512!=fat_cached) {
            fat_cached=at/512;
            if(!read_sector(reserved+fat_cached,1,sector)) return 0;
        }
        next=extent_u16(sector+(at&511));
        if(i+1==need ? next<0xfff8 : next!=cluster+1) return 0;
    }
    unsigned first=data+(start-2)*spc;
    if(file_bytes/512>total-first || !read_sector(first,1,sector) ||
       memcmp(sector,header,512)) return 0;
    *first_sector=first;
    return 1;
}
#endif
