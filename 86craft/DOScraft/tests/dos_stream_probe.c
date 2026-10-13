/* Guest PIT measurement of cold/backward terrain reads, outside rendering.
 * Run only on a private writable test image; writes use an unused edit slot. */
#include "sys.h"
#include "system.h"
#include "hdd.h"
#include "hdd_backend.h"
#include "bench_clock.h"
#include <pc.h>
#include <stdio.h>
#include <string.h>
static unsigned ticks0,count0;
static unsigned long long elapsed;
static unsigned checksum[4096];
static unsigned char buffer[12288],saved[12288];
static unsigned times[16],peaks[16],write_time,write_peak;
static unsigned ids[16]={2080,2081,2144,2080,2079,2016,2145,2080,
                         2081,2080,2144,2016,2079,2080,0,4095};
static unsigned clock_us(void)
{
    unsigned t,c;
    do { t=g_ticks; outportb(0x43,0x80); c=inportb(0x42); c|=inportb(0x42)<<8; } while(t!=g_ticks);
    elapsed+=dos_bench_clock_delta(t,ticks0,c,count0); ticks0=t; count0=c;
    return (unsigned)(elapsed*1000000u/1193182u);
}
static int measured_io(unsigned lba,int write,unsigned *time,unsigned *peak)
{
    unsigned begin=clock_us(); int status;
    *peak=0;
    do {
        unsigned mark=clock_us();
        status=hdd_transfer(lba,24,write,buffer);
        unsigned duration=clock_us()-mark;
        if(duration>*peak) *peak=duration;
    } while(!status);
    *time=clock_us()-begin;
    return status==1;
}
int main(void)
{
    int ok=1,old61,i;
    sys_init();
    if(!hdd_init() || hdd_transfer_sync(0,1,0,buffer)!=1 ||
       memcmp(buffer,"DOSCRAFT-1024-V1",16)) return 1;
    for(i=0;i<32;i+=24) {
        unsigned n=i==0?24:8;
        if(hdd_transfer_sync(1+i,n,0,buffer)!=1) return 1;
        memcpy((unsigned char *)checksum+i*512,buffer,n*512);
    }
    old61=inportb(0x61); outportb(0x61,(old61|1)&~2);
    outportb(0x43,0xb4); outportb(0x42,0); outportb(0x42,0);
    ticks0=g_ticks; outportb(0x43,0x80); count0=inportb(0x42); count0|=inportb(0x42)<<8;
    for(i=0;i<16;++i) {
        unsigned sum=2166136261u;
        if(!measured_io(33+ids[i]*72,0,&times[i],&peaks[i])) { ok=0; break; }
        for(int j=0;j<12288;++j) sum=(sum^buffer[j])*16777619u;
        if(sum!=checksum[ids[i]]) { ok=0; break; }
    }
    memcpy(saved,buffer,sizeof(saved));
    if(ok && !measured_io(33+4095*72+24,1,&write_time,&write_peak)) ok=0;
    if(ok && (hdd_transfer_sync(33+4095*72+24,24,0,buffer)!=1 || memcmp(saved,buffer,sizeof(saved)))) ok=0;
    dos_hdd_shutdown(); outportb(0x61,old61); dos_system_shutdown();
    FILE *file=fopen("C:\\IOPROBE.TXT","wb");
    if(!file) return 1;
    fprintf(file,"RESULT=%s\n",ok?"PASS":"FAIL");
    for(i=0;i<16;++i) fprintf(file,"READ=%u,%u,%u\n",ids[i],times[i],peaks[i]);
    fprintf(file,"WRITE=%u,%u\n",write_time,write_peak);
    return fclose(file) || !ok;
}
