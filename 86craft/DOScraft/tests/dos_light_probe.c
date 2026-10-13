/* Actual cache/light implementation, PIT timed, with no render or UI input. */
#include "world.c"
#include "sys.h"
#include "system.h"
#include "hdd_backend.h"
#include "bench_clock.h"
#include <pc.h>
#include <stdio.h>
static unsigned ticks0,count0;
static unsigned long long elapsed;
static unsigned calls[12],times[12],peaks[12];
static unsigned clock_us(void)
{
    unsigned t,c;
    do { t=g_ticks; outportb(0x43,0x80); c=inportb(0x42); c|=inportb(0x42)<<8; } while(t!=g_ticks);
    elapsed+=dos_bench_clock_delta(t,ticks0,c,count0); ticks0=t; count0=c;
    return (unsigned)(elapsed*1000000u/1193182u);
}
static int settled(int x,int z)
{
    if(cache_light_pending() || pqHead!=pqTail || streamError) return 0;
    int cx=x/CS,cz=z/CS;
    for(int nz=cz-2;nz<=cz+2;++nz) for(int nx=cx-2;nx<=cx+2;++nx)
        if(!world_column_sim_ready(nx,nz)) return 0;
    return 1;
}
int main(void)
{
    sys_init(); world_alloc(); world_generate(4242);
    int old61=inportb(0x61),ok=1;
    outportb(0x61,(old61|1)&~2); outportb(0x43,0xb4); outportb(0x42,0); outportb(0x42,0);
    ticks0=g_ticks; outportb(0x43,0x80); count0=inportb(0x42); count0|=inportb(0x42)<<8;
    for(int route=0;route<12;++route) {
        int step=route<7 ? route : 12-route;
        /* Stay at cell-center so reverse travel's four-block prefetch does
         * not shift the resident window away from the region we verify. */
        int x=(g_spawnX/CS)*CS+CS/2+step*CS,z=(g_spawnZ/CS)*CS+CS/2;
        unsigned start=clock_us();
        do {
            unsigned begin=clock_us(); cache_stream(x,z,1);
            unsigned duration=clock_us()-begin;
            if(duration>peaks[route]) peaks[route]=duration;
            if(++calls[route]>5000 || streamError) { ok=0; break; }
        } while(!settled(x,z));
        times[route]=clock_us()-start;
        if(!ok) break;
    }
    dos_hdd_shutdown(); outportb(0x61,old61); dos_system_shutdown();
    FILE *file=fopen("C:\\IOPROBE.TXT","wb"); if(!file) return 1;
    fprintf(file,"RESULT=%s\nNOTE=world cache/lighting only; no rendering or host input\n",ok?"PASS":"FAIL");
    for(int i=0;i<12;++i) fprintf(file,"LIGHT_ROUTE=%u,%u,%u,%u\n",i,calls[i],times[i],peaks[i]);
#ifdef DOS_STREAM_LIGHT_CACHE
    fprintf(file,"LIGHT_RESTORE_STATS=%u,%u,%u,%u\n",g_streamLightStats[0],g_streamLightStats[1],g_streamLightStats[2],g_streamLightStats[3]);
#endif
    return fclose(file) || !ok;
}
