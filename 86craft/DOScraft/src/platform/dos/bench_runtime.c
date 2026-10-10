/* Reproducible graphics/world-update replay. No Windows/AT key injection.
 * Simulation is frozen to compare identical geometry at identical poses;
 * this is NOT a physics/gameplay-throughput test. Normal builds exclude it. */
#include "bench.h"
#include "bench_transport.h"
#include "sys.h"
#include "world.h"
#include "player.h"
#include "render.h"
#include "game.h"
#include "gfx.h"
#include "fmath.h"
#include "timer_phase.h"
#include "bench_clock.h"
#include "system.h"
#include "hdd_backend.h"
#include "save_backend.h"
#include "video_backend.h"
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>

#define PHASES 6
#define FRAMES 32
#define WARM 6
u32 g_benchOut[BO_COUNT],g_benchSec[2][S_N],g_benchCnt[S_N];
int g_benchFreeze=1;
unsigned int dos_bench_pack,dos_bench_upload,dos_bench_wait,dos_bench_timeouts;
static unsigned int times[PHASES][FRAMES],sections[PHASES][10];
static unsigned int packs[PHASES],uploads[PHASES],waits[PHASES],timeouts[PHASES];
static unsigned int pixels[PHASES],faces[PHASES],items[PHASES],hashes[PHASES];
static unsigned int tick0,lo0,acc,secmark,last;
static int clock_on,old61,phase=-1,frame,measured,run=1,vga_ok=1,sx,sy,sz;
static const char *names[PHASES]={"open_static","walk","walk_down","walk_up","house","block_edits"};

u32 bench_us(void)
{
    unsigned int tick,lo;
    if(!clock_on) {
        old61=inportb(0x61); outportb(0x61,(old61|1)&~2);
        outportb(0x43,0xb4); outportb(0x42,0); outportb(0x42,0);
        tick0=g_ticks; outportb(0x43,0x80);
        lo0=inportb(0x42); lo0|=inportb(0x42)<<8;
        clock_on=1;
    }
    /* Read stable IRQ epoch around the independently running channel-2 latch.
     * IRQ0 stays at 100Hz. Nearest 65536-count wrap is resolved by that epoch;
     * its <=10ms uncertainty is < half a channel-2 period (27.46ms). */
    do {
        tick=g_ticks; outportb(0x43,0x80);
        lo=inportb(0x42); lo|=inportb(0x42)<<8;
    } while(tick!=g_ticks);
    acc+=dos_bench_clock_delta(tick,tick0,lo,lo0); lo0=lo; tick0=tick;
    return (u32)((unsigned long long)acc*1000000u/1193182u);
}
void bench_gen_begin(void) { g_benchOut[BO_GEN_MS]=bench_us(); }
void bench_gen_end(void) { g_benchOut[BO_GEN_MS]=(bench_us()-g_benchOut[BO_GEN_MS])/1000; }
void bench_update_begin(void) {}
void bench_update_end(void) {}
void bench_collect(u32 us) { (void)us; }
void bench_mark(int section)
{
    u32 now=bench_us();
    if(measured && section<10) sections[phase][section]+=now-secmark;
    secmark=now;
}
void dos_game_options(int argc,char **argv)
{
    if(argc==2 && argv[1][0]=='/' && argv[1][1]>='1' && argv[1][1]<='3') run=argv[1][1]-'0';
}
void __real_gfx_present(void);
void __wrap_gfx_present(void)
{
    const u8 *fb=g_fb;
    __real_gfx_present();
    if(phase>=0 && frame==WARM+FRAMES-1) {
        unsigned int h=2166136261u; int x,y;
        /* Correctness readback/hash is after the timing boundary; excluded
         * from this and next frame. All three hidden pages cycle normally. */
        bench_mark(S_PRESENT);
        if(dos_video_verify(fb,512)) vga_ok=0;
        for(y=0;y<240;++y) for(x=0;x<320;++x) h=(h^fb[y*512+x])*16777619u;
        hashes[phase]=h;
        measured=0;
    }
}
static void house(void)
{
    int x,y,z;
    /* Fixture only: 7x7 closed roof/walls with a doorway, same seed/lighting
     * rules and real world_set incremental edit/mesh invalidations. */
    for(y=sy;y<sy+5;++y) for(z=sz-3;z<=sz+3;++z) for(x=sx-3;x<=sx+3;++x) {
        int solid=y==sy || y==sy+4 || x==sx-3 || x==sx+3 || z==sz-3 || z==sz+3;
        if(z==sz-3 && x==sx && y>sy && y<sy+3) solid=0;
        world_set(x,y,z,solid?B_STONE:B_AIR);
    }
}
static void finish(void)
{
    int p,f,s; char path[]="C:\\BENCH1.TXT"; FILE *file;
    int ok=vga_ok && !world_stream_error();
    unsigned int ticks=g_ticks;
    dos_save_shutdown(); dos_hdd_shutdown(); dos_system_shutdown(); dos_video_restore();
    outportb(0x61,old61);
    path[8]=(char)('0'+run); file=fopen(path,"wb"); if(!file) exit(1);
    fprintf(file,"VERSION=1\nSEED=4242\nSIMULATION=FROZEN_GRAPHICS_REPLAY\nSCALE=%d\nVIEW=%d\nFLAT=%d\nSUBDIV=%d\nOCCLUSION=%d\nGEN_MS=%u\nTICKS=%u\nRESULT=%s\n",
        g_renderScale,g_viewDist,g_flatDist,g_textureSubdivision,g_occlusion,g_benchOut[BO_GEN_MS],ticks,ok?"PASS":"FAIL");
    for(p=0;p<PHASES;++p) {
        fprintf(file,"SCENE=%s\nPACK=%u\nUPLOAD=%u\nWAIT=%u\nTIMEOUTS=%u\nPIXELS=%u\nFACES=%u\nITEMS=%u\nHASH=%08x\nFRAMES=",names[p],packs[p],uploads[p],waits[p],timeouts[p],pixels[p],faces[p],items[p],hashes[p]);
        for(f=0;f<FRAMES;++f) fprintf(file,"%s%u",f?",":"",times[p][f]);
        fprintf(file,"\nSECTIONS=");
        for(s=0;s<10;++s) fprintf(file,"%s%u",s?",":"",sections[p][s]);
        fprintf(file,"\n");
    }
    if(ferror(file) || fclose(file)) exit(1);
    path[10]='E'; path[11]='N'; path[12]='D';
    file=fopen(path,"wb"); if(!file) exit(1);
    if(fprintf(file,"%s\n",ok?"PASS":"FAIL")<0 || fclose(file)) exit(1);
    puts("DOScraft benchmark complete; safe to close this private VM."); exit(!ok);
}
void bench_frame(void)
{
    extern u32 g_statPixels;
    unsigned int now=bench_us();
    if(measured) {
        int n=frame-WARM;
        bench_mark(S_PRESENT);
        times[phase][n]=now-last;
        packs[phase]+=dos_bench_pack; uploads[phase]+=dos_bench_upload;
        waits[phase]+=dos_bench_wait; timeouts[phase]+=dos_bench_timeouts;
        pixels[phase]+=g_statPixels; faces[phase]+=g_statFaces; items[phase]+=g_statItems;
    } else if(phase>=0 && frame==WARM+FRAMES-1) {
        /* Last frame was closed before readback. Include its real duration
         * and transport counters, but never the diagnostic readback time. */
        times[phase][FRAMES-1]=secmark-last;
        packs[phase]+=dos_bench_pack; uploads[phase]+=dos_bench_upload;
        waits[phase]+=dos_bench_wait; timeouts[phase]+=dos_bench_timeouts;
        pixels[phase]+=g_statPixels; faces[phase]+=g_statFaces; items[phase]+=g_statItems;
    }
    if(phase<0) {
        sx=g_player.body.x/FU; sz=g_player.body.z/FU; sy=world_surface_y(sx,sz);
        g_renderScale=2; g_viewDist=8; g_flatDist=0; g_distanceMode=0;
#ifdef DOS_BENCH_OCCLUSION
        g_occlusion=1;
#endif
        phase=0; frame=-1;
    }
    if(++frame==WARM+FRAMES) {
        frame=0; if(++phase==PHASES) finish();
        if(phase==4) house();
    }
    g_time=6000; g_skyDarken=0;
    g_player.body.x=sx*FU+FU/2; g_player.body.z=(sz-6)*FU+FU/2;
    g_player.body.y=(sy+1)*FU; g_player.yaw=0; g_player.pitch=-40;
    if(phase>=1 && phase<=3) {
        g_player.body.x+=(frame*FU/8); g_player.yaw=frame*8;
        g_player.body.y=(world_surface_y(g_player.body.x/FU,g_player.body.z/FU)+1)*FU;
        g_player.pitch=phase==2?-250:phase==3?250:-40;
    }
    if(phase>=4) {
        g_player.body.z=sz*FU+FU/2; g_player.body.y=(sy+1)*FU; g_player.pitch=-20;
        g_player.yaw=frame*16;
        if(phase==5) world_set(sx+2,sy+2,sz,(frame&1)?B_AIR:B_STONE);
    }
    g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
    measured=frame>=WARM; dos_bench_pack=dos_bench_upload=dos_bench_wait=dos_bench_timeouts=0;
    g_statPixels=0; last=secmark=bench_us();
}
