/* INT 25h/26h use DOS's existing block driver/SCSI BIOS. The real-mode
 * wrapper discards their extra flags word before returning to DPMI.
 * This bypasses repeated FAT-chain searches; each transfer is synchronous.
 * Direct writes are restricted by hdd.c to the validated file's edit slots;
 * header, immutable base columns, filesystem metadata and saves are excluded. */
#include <dpmi.h>
#include <sys/movedata.h>
#include <string.h>
#include "hdd_extent.h"
static int extent_selector=-1,extent_segment,extent_ready;
static unsigned extent_first;
static int extent_transfer(unsigned sector,unsigned count,int write,unsigned char *dst)
{
    unsigned char packet[10];
    __dpmi_regs regs={0};
    unsigned address=(unsigned)extent_segment<<4;
    unsigned char interrupt=write ? 0x26 : 0x25;
    if(!count || count>4 || extent_selector<0) return 0;
    memcpy(packet,&sector,4);
    packet[4]=count; packet[5]=0;
    packet[6]=32; packet[7]=0;
    packet[8]=extent_segment; packet[9]=extent_segment>>8;
    dosmemput(packet,sizeof(packet),address+16);
    dosmemput(&interrupt,1,address+4);
    if(write) dosmemput(dst,count*512,address+32);
    regs.x.ax=2; regs.x.cx=0xffff; regs.x.ds=extent_segment; regs.x.bx=16;
    regs.x.cs=extent_segment; regs.x.ip=0;
    if(__dpmi_simulate_real_mode_procedure_retf(&regs) || (regs.x.flags&1)) return 0;
    if(!write) dosmemget(address+32,count*512,dst);
    return 1;
}
static int extent_read(unsigned sector,unsigned count,unsigned char *dst)
{ return extent_transfer(sector,count,0,dst); }
static void extent_shutdown(void)
{
    extent_ready=0;
    if(extent_selector>=0) __dpmi_free_dos_memory(extent_selector);
    extent_selector=-1;
}
static void extent_init(unsigned bytes)
{
    static const unsigned char code[]={0x55,0x89,0xe5,0xcd,0x25,0x89,0xec,0x5d,0xcb};
    unsigned char header[512];
    /* Only the known large medium is eligible. Its file handle remains open
     * for commit and as the fallback for a fragmented filesystem. After the
     * switch, no buffered file I/O touches terrain payloads: mixing them with
     * absolute writes could otherwise leave stale DOS data buffers. */
    if(lseek(terrain_fd,0,SEEK_SET)!=0 || read(terrain_fd,header,512)!=512 ||
       !dos_terrain_known_medium(bytes,header)) return;
    extent_segment=__dpmi_allocate_dos_memory((32+2048)/16,&extent_selector);
    if(extent_segment<0) { extent_selector=-1; return; }
    dosmemput(code,sizeof(code),(unsigned)extent_segment<<4);
    if(!dos_terrain_find_extent(extent_read,bytes,header,&extent_first)) { extent_shutdown(); return; }
    extent_ready=1;
}
