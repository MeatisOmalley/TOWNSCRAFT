/* Reproducible graphics/world-update replay. No Windows/AT key injection.
 * Simulation is frozen to compare identical geometry at identical poses;
 * this is NOT a physics/gameplay-throughput test. Normal builds exclude it. */
#include "bench.h"
#ifdef DOS_BENCH_PROFILE_CW
#include "bench_profile_cw.h"
#endif
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
#ifdef DOS_GAME_AUDIO
#include "sound.h"
#include "game_audio.h"
#include "sb_pcm.h"
#endif
#ifdef DOS_VIEWMODEL
#include "viewmodel.h"
#endif
#ifdef DOS_BENCH_VIEWMODEL_CASES
#include "inventory.h"
#endif
#ifdef DOS_FAR_FIELD
#include "far_field.h"
#endif
#ifdef DOS_BENCH_PROFILE_FAR
extern u32 dos_far_us[3];
static u32 farTimes[6][3];
#endif
#ifdef DOS_RASTER_FINISH_SKIP
extern void raster_complete_odd_rows(void);
#endif
#ifdef DOS_BENCH_QUALITY
#include "graphics_presets.h"
#endif
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DOS_BENCH_PROFILE_DRAW
extern u32 g_profSamples[4096],g_profCount;
extern int g_profEnable;
#endif
#ifdef DOS_FRONT_TO_BACK
#include "f2b_raster.h"
extern unsigned dos_f2b_fallbacks,dos_f2b_full_stops;
#ifdef DOS_F2B_VERIFY
extern unsigned dos_f2b_verify_failures,dos_f2b_verify_frames,dos_f2b_verify_fallbacks;
#endif
#endif

#define PHASES 6
#ifndef DOS_BENCH_FRAMES
#define DOS_BENCH_FRAMES 32
#endif
#if DOS_BENCH_FRAMES < 32 || DOS_BENCH_FRAMES > 10000
#error Unsupported benchmark sample count
#endif
#define FRAMES DOS_BENCH_FRAMES
#define WARM 6
#ifdef DOS_BENCH_DIAGONAL_ROUTE
#define phase_frames(p) ((p)==1 ? FRAMES : 32)
#else
#define phase_frames(p) FRAMES
#endif
/* Long diagnostics can exceed 32-bit PIT counts and accumulated microseconds.
 * Preserve short-run arithmetic/codegen for existing comparison checkpoints. */
#if FRAMES > 32
typedef unsigned long long BenchTotal;
#else
typedef unsigned int BenchTotal;
#endif
#ifdef DOS_FRONT_TO_BACK
static unsigned f2b_stats[PHASES][8];
static void record_f2b(int p)
{
    f2b_stats[p][0]+=dos_f2b_stats.sampled;
    f2b_stats[p][1]+=dos_f2b_stats.skipped;
    f2b_stats[p][2]+=dos_f2b_stats.rows;
    f2b_stats[p][3]+=dos_f2b_stats.hidden_rows;
    f2b_stats[p][4]+=dos_f2b_stats.hidden_primitives;
    f2b_stats[p][5]+=dos_f2b_stats.covered;
    f2b_stats[p][6]+=dos_f2b_fallbacks;
    f2b_stats[p][7]+=dos_f2b_full_stops;
}
#endif
u32 g_benchOut[BO_COUNT],g_benchSec[2][S_N],g_benchCnt[S_N];
int g_benchFreeze=1;
unsigned int dos_bench_pack,dos_bench_upload,dos_bench_wait,dos_bench_timeouts;
static unsigned int times[PHASES][FRAMES];
static BenchTotal sections[PHASES][10];
#ifdef DOS_VIEWMODEL
static BenchTotal viewmodelTimes[PHASES];
#endif
static unsigned int edit_calls[PHASES][FRAMES];
static BenchTotal packs[PHASES],uploads[PHASES],waits[PHASES],timeouts[PHASES];
static BenchTotal pixels[PHASES],faces[PHASES],items[PHASES];
static unsigned int hashes[PHASES];
#ifdef DOS_BENCH_PROFILE_CW
static BenchTotal cw_times[PHASES][CW_N],cw_calls[PHASES][CW_N];
static BenchTotal cw_reasons[PHASES][CW_REASON_N];
#endif
#ifdef DOS_WALL_VISIBILITY
extern unsigned dos_wall_sources,dos_wall_tests,dos_wall_rejected;
static unsigned wall_stats[PHASES][FRAMES][3];
#ifdef DOS_WALL_VERIFY
extern unsigned dos_wall_verify_frames,dos_wall_verify_failures;
#endif
#endif
#ifdef DOS_BENCH_PROFILE_DRAW
static unsigned profile_begin[PHASES],profile_end[PHASES];
#endif
static unsigned int tick0,lo0,secmark,last;
static BenchTotal acc;
static int clock_on,old61,phase=-1,frame,measured,run=1,suite,sweep,suite_warmup,vga_ok=1,sx,sy,sz;
static u8 house_saved[5][7][7];
static const char *names[PHASES]={"open_static","walk","walk_down","walk_up","house","block_edits"};

#ifdef DOS_BENCH_PROFILE_CW
void bench_cw_time(int section,u32 us)
{
    if(measured && phase>=0 && phase<PHASES && section>=0 && section<CW_N) {
        cw_times[phase][section]+=us;
        ++cw_calls[phase][section];
    }
}
void bench_cw_reason(unsigned mask)
{
    int i;
    if(!measured || phase<0 || phase>=PHASES) return;
    if(!mask) { ++cw_reasons[phase][CW_HIT]; return; }
    for(i=0;i<CW_HIT;++i) if(mask&(1u<<i)) ++cw_reasons[phase][i];
}
#endif

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
void bench_gen_begin(void) {
    g_benchOut[BO_GEN_MS]=bench_us();
#ifdef DOS_BENCH_PROFILE_DRAW
    /* new_game inherited an unconditional generation-profiler enable.
     * This diagnostic owns drawing samples, not generation; disable before
     * generation and reset its bounded ring before any frame starts. */
    g_profEnable=0; g_profCount=0;
#endif
}
void bench_gen_end(void) { g_benchOut[BO_GEN_MS]=(bench_us()-g_benchOut[BO_GEN_MS])/1000; }
void bench_update_begin(void) {}
void bench_update_end(void) {}
void bench_collect(u32 us) { (void)us; }
void bench_mark(int section)
{
    u32 now=bench_us();
    if(measured && section<10) sections[phase][section]+=now-secmark;
    secmark=now;
#ifdef DOS_BENCH_PROFILE_DRAW
    g_profEnable=section==S_SKY;
#endif
}
void dos_game_options(int argc,char **argv)
{
#ifdef DOS_BENCH_FAR_LEVEL
    dos_far_enabled=DOS_BENCH_FAR_LEVEL;
#endif
    if(argc==2 && argv[1][0]=='/' && argv[1][1]>='1' && argv[1][1]<='3') run=argv[1][1]-'0';
    if(argc==2 && !strcmp(argv[1],"/ALL")) suite=suite_warmup=1;
    if(argc==2 && !strcmp(argv[1],"/SWEEP")) suite=sweep=suite_warmup=1;
}
void dos_game_inventory_idle(void) {}
#ifdef DOS_BENCH_DUMP_FRAMES
static void bench_dump_frame(const char *path,const u8 *fb)
{
    unsigned char palette[768];
    outportb(0x3c7,0);
    for(int x=0;x<768;++x) palette[x]=(unsigned char)(inportb(0x3c9)<<2);
    FILE *dump=fopen(path,"wb");
    if(!dump) { vga_ok=0; return; }
    if(fwrite(palette,1,sizeof(palette),dump)!=sizeof(palette)) vga_ok=0;
    for(int y=0;y<240;++y) if(fwrite(fb+y*512,1,320,dump)!=320) vga_ok=0;
    if(fclose(dump)) vga_ok=0;
}
#endif
void __real_gfx_present(void);
void __wrap_gfx_present(void)
{
#ifdef DOS_BENCH_AUDIO_STRESS
    /* Independent busy-effects fixture; no gameplay/random/world changes. */
    if(phase>=0) {
        sound_loop(SFX_RAIN,100);
        if(!(frame&7))sound_play(SFX_STEP,256,180,0);
        if(!(frame&15))sound_play(SFX_STONE,256,190,0);
    }
#endif
    const u8 *fb=g_fb;
    __real_gfx_present();
#ifdef DOS_VIEWMODEL
    if(measured && phase>=0) viewmodelTimes[phase]+=dos_viewmodel_us;
#endif
#ifdef DOS_BENCH_PROFILE_FAR
    if(measured && phase>=0) for(int i=0;i<3;++i) farTimes[phase][i]+=dos_far_us[i];
#endif
    if(phase>=0 && frame==WARM+phase_frames(phase)-1) {
        unsigned int h=2166136261u; int x,y;
        /* Correctness readback/hash is after the timing boundary; excluded
         * from this and next frame. All three hidden pages cycle normally. */
        bench_mark(S_PRESENT);
#ifdef DOS_RASTER_FINISH_SKIP
        raster_complete_odd_rows();
#endif
        if(dos_video_verify(fb,512)) vga_ok=0;
#ifdef DOS_VERY_LOW
        dos_video_complete_very_low_source((u8 *)fb,512);
#endif
        for(y=0;y<240;++y) for(x=0;x<320;++x) h=(h^fb[y*512+x])*16777619u;
        hashes[phase]=h;
        measured=0;
#ifdef DOS_BENCH_DUMP_FRAMES
        /* Diagnostic only, after closing the frame clock. Disk/IRQ time can
         * perturb later scheduling, so these runs are not gain evidence. */
        {
            char path[]="C:\\B1S0.RAW";
            path[4]=(char)('0'+run); path[6]=(char)('0'+phase);
            bench_dump_frame(path,fb);
        }
#endif
    }
}
static void house(void)
{
    int x,y,z;
    /* Fixture only: 7x7 closed roof/walls with a doorway, same seed/lighting
     * rules and real world_set incremental edit/mesh invalidations. */
    for(y=sy;y<sy+5;++y) for(z=sz-3;z<=sz+3;++z) for(x=sx-3;x<=sx+3;++x) {
        int solid=y==sy || y==sy+4 || x==sx-3 || x==sx+3 || z==sz-3 || z==sz+3;
        if(run==1) house_saved[y-sy][z-sz+3][x-sx+3]=wget(x,y,z);
        if(z==sz-3 && x==sx && y>sy && y<sy+3) solid=0;
        world_set(x,y,z,solid?B_STONE:B_AIR);
    }
#ifdef DOS_F2B_STRESS
    /* Verification-only mixed transparency/model fixture, never benchmark
     * performance evidence or playable terrain/gameplay changes. */
    world_set(sx,sy+1,sz+2,B_TORCH);
    world_set(sx+1,sy+1,sz+2,B_FLOWER);
    world_set(sx+2,sy+1,sz+2,B_TALLGRASS);
    world_set(sx-2,sy+1,sz+2,B_WATER);
    world_set(sx-1,sy+1,sz+2,B_GLASS);
    world_set(sx+3,sy+2,sz,B_LEAVES);
    world_set(sx-3,sy+1,sz,MKBLK(B_DOOR_LOWER,2));
    world_set(sx-3,sy+2,sz,MKBLK(B_DOOR_UPPER,2));
#endif
}
static void restore_house(void)
{
    int x,y,z;
    for(y=sy;y<sy+5;++y) for(z=sz-3;z<=sz+3;++z) for(x=sx-3;x<=sx+3;++x)
        world_set(x,y,z,house_saved[y-sy][z-sz+3][x-sx+3]);
}
static void select_quality(int index)
{
    g_renderScale=2; g_viewDist=8; g_flatDist=0; g_distanceMode=0;
#ifdef DOS_BENCH_QUALITY
#ifdef DOS_BENCH_FAR_SWEEP
    dos_far_enabled=index+DOS_FAR_LOW;
    index=DOS_GRAPHICS_LOW;
#endif
    const DosGraphicsSettings *quality=dos_graphics_settings(index);
    g_renderScale=quality->render_scale; g_viewDist=quality->view_distance;
    g_flatDist=quality->flat_distance; g_distanceMode=quality->distance_mode;
    g_textureSubdivision=quality->texture_subdivision;
    dos_very_low_mode=index==DOS_GRAPHICS_VERY_LOW;
    dos_potato_mode=0;
#ifdef DOS_BENCH_RESOLUTION
    g_renderScale=dos_resolution_set(DOS_BENCH_RESOLUTION);
#endif
#ifdef DOS_BENCH_A3_CEILING
    /* A3's best possible geometry saving, before paying for perspective UVs. */
    g_textureSubdivision=SUBDIV_OFF;
#endif
#else
    (void)index;
#endif
#ifdef DOS_BENCH_TEXTURED_DISTANCE
    /* Exercise the same shared distance representation as the graphics menu. */
#ifdef DOS_BENCH_FLAT_DISTANCE
    g_viewDist=CLAMP(DOS_BENCH_FLAT_DISTANCE,4,DOS_GRAPHICS_MAX_DISTANCE);
#endif
    int textured=CLAMP(DOS_BENCH_TEXTURED_DISTANCE,0,DOS_GRAPHICS_MAX_DISTANCE);
    if(g_viewDist<textured) g_viewDist=textured;
    g_flatDist=dos_flat_cutoff(textured,g_viewDist);
#endif
}
static void clear_samples(void)
{
    memset(times,0,sizeof(times)); memset(sections,0,sizeof(sections));
    memset(edit_calls,0,sizeof(edit_calls)); memset(packs,0,sizeof(packs));
    memset(uploads,0,sizeof(uploads)); memset(waits,0,sizeof(waits));
    memset(timeouts,0,sizeof(timeouts)); memset(pixels,0,sizeof(pixels));
    memset(faces,0,sizeof(faces)); memset(items,0,sizeof(items));
    memset(hashes,0,sizeof(hashes));
#ifdef DOS_VIEWMODEL
    memset(viewmodelTimes,0,sizeof(viewmodelTimes));
#endif
#ifdef DOS_BENCH_PROFILE_CW
    memset(cw_times,0,sizeof(cw_times)); memset(cw_calls,0,sizeof(cw_calls));
    memset(cw_reasons,0,sizeof(cw_reasons));
#endif
#ifdef DOS_WALL_VISIBILITY
    memset(wall_stats,0,sizeof(wall_stats));
#endif
#ifdef DOS_FRONT_TO_BACK
    memset(f2b_stats,0,sizeof(f2b_stats));
#endif
}
static void finish(void)
{
    int p,f,s; char path[]="C:\\BENCH1.TXT"; FILE *file;
    int ok=vga_ok && !world_stream_error();
#ifdef DOS_COLLECT_VERIFY
    extern unsigned dos_collect_verify_frames,dos_collect_verify_failures;
    ok=ok && dos_collect_verify_frames && !dos_collect_verify_failures;
#endif
#ifdef DOS_WALL_VERIFY
    ok=ok && !dos_wall_verify_failures && dos_wall_verify_frames==PHASES*(WARM+phase_frames(phase));
#endif
#ifdef DOS_F2B_VERIFY
    ok=ok && !dos_f2b_verify_failures &&
        dos_f2b_verify_frames+dos_f2b_verify_fallbacks==PHASES*(WARM+phase_frames(phase));
#ifdef DOS_F2B_STRESS
    ok=ok && dos_f2b_verify_fallbacks==1;
#else
    ok=ok && !dos_f2b_verify_fallbacks;
#endif
#endif
    if(suite_warmup && ok) {
        /* The house/edit fixture leaves streamed mesh work behind. Do one
         * unreported pass, then all three measured runs share that state. */
        restore_house();
        while(world_update_dirty_chunks(1000)) {}
        world_stream(sx,sz-6,g_viewDist+8,1000);
        clear_samples();
        phase=0; frame=0; suite_warmup=0;
        return;
    }
    unsigned int ticks=g_ticks;
#ifdef DOS_GAME_AUDIO
    const char *audio_backend=dos_audio_backend();
    unsigned int audio_events=dos_audio_events,audio_blocks=dos_sb_completions,audio_faults=dos_sb_stream_faults;
    ok=ok && !strcmp(audio_backend,"SB_SAMPLED") && !audio_faults;
#endif
    if(!suite || run==3 || !ok) {
        dos_save_shutdown(); dos_hdd_shutdown(); dos_system_shutdown(); dos_video_restore();
        outportb(0x61,old61);
    }
    path[8]=(char)('0'+run); file=fopen(path,"wb"); if(!file) exit(1);
    fprintf(file,"VERSION=2\nEDIT_TIMING=CALL_PLUS_FOLLOWING_FRAME\nSEED=4242\nSIMULATION=FROZEN_GRAPHICS_REPLAY\nSCALE=%d\nVIEW=%d\nFLAT=%d\nSUBDIV=%d\nOCCLUSION=%d\nGEN_MS=%u\nTICKS=%u\nRESULT=%s\n",
        g_renderScale,g_viewDist,g_flatDist,g_textureSubdivision,g_occlusion,g_benchOut[BO_GEN_MS],ticks,ok?"PASS":"FAIL");
    fprintf(file,"REPLAY_CONTEXT=%s\n",suite?"POST_FIXTURE":"FRESH_WORLD");
#ifdef DOS_GAME_AUDIO
    fprintf(file,"AUDIO=%s\nAUDIO_EVENTS=%u\nAUDIO_BLOCKS=%u\nAUDIO_FAULTS=%u\n",audio_backend,audio_events,audio_blocks,audio_faults);
#else
    fprintf(file,"AUDIO=OFF\n");
#endif
    fprintf(file,"SAMPLES_PER_SCENE=%d\n",FRAMES);
#ifdef DOS_BENCH_VIEWMODEL_CASES
    fprintf(file,"HELD_CASE=%s\n",run==1 ? "empty_hand" : run==2 ? "cobblestone" : "iron_pickaxe");
#endif
#ifdef DOS_BENCH_DIAGONAL_ROUTE
    fprintf(file,"MOTION_POLICY=CONTINUOUS_WALK_OTHER_SCENES_32_POSES\nWALK_ROUTE=CONTINUOUS_DIAGONAL_XZ_FIXED_YAW_45_HORIZON\n");
#else
    fprintf(file,"MOTION_POLICY=REPEAT_32_MEASURED_POSES\n");
#endif
#ifdef DOS_FAR_FIELD
    fprintf(file,"DISTANT_LEVEL=%d\n",dos_far_enabled);
#endif
#ifdef DOS_BENCH_FAR_SWEEP
    fprintf(file,"FAR_SWEEP=LOW_MEDIUM_HIGH_WITH_LOW_MAIN_GRAPHICS\n");
#endif
#ifdef DOS_WALL_VERIFY
    fprintf(file,"WALL_VERIFICATION=TIMINGS_INVALID\nWALL_VERIFY_FRAMES=%u\nWALL_VERIFY_FAILURES=%u\n",dos_wall_verify_frames,dos_wall_verify_failures);
#endif
#ifdef DOS_BENCH_DUMP_FRAMES
    fprintf(file,"FRAME_DUMPS=TIMINGS_DIAGNOSTIC_ONLY\n");
#endif
#ifdef DOS_VGA_SCAN_QUAD
    {
        extern volatile unsigned int dos_raster_frames,dos_raster_quad_frames,dos_raster_missed_syncs,
            dos_raster_split_max_line,dos_raster_split_late,dos_raster_unlocked;
        extern unsigned int dos_quad_frames,dos_quad_transitions;
        fprintf(file,"SCAN_QUAD=%u,%u,%u,%u,%u,%u,%u,%u\n",dos_raster_frames,dos_raster_quad_frames,
                dos_raster_missed_syncs,dos_raster_split_max_line,dos_raster_split_late,
                dos_quad_frames,dos_quad_transitions,dos_raster_unlocked);
    }
#endif
#ifdef DOS_F2B_VERIFY
    fprintf(file,"F2B_VERIFICATION=TIMINGS_INVALID\nF2B_VERIFY_FRAMES=%u\nF2B_VERIFY_FAILURES=%u\nF2B_VERIFY_FALLBACKS=%u\n",dos_f2b_verify_frames,dos_f2b_verify_failures,dos_f2b_verify_fallbacks);
#endif
#ifdef DOS_BENCH_PROFILE_DRAW
    fprintf(file,"DRAW_PROFILING=TIMINGS_DIAGNOSTIC_ONLY\nDRAW_SCOPE=POST_GENERATION_SECTION_GATED\nDRAW_EIP=");
    for(s=0;s<(int)g_profCount;++s) fprintf(file,"%s%08x",s?",":"",g_profSamples[s]);
    fprintf(file,"\n");
#endif
#ifdef DOS_BENCH_PROFILE_CW
    fprintf(file,"CW_PROFILING=BENCHMARK_ONLY_INSTRUMENTED_TIMINGS\n");
#endif
    for(p=0;p<PHASES;++p) {
        fprintf(file,"SCENE=%s\nSAMPLES=%d\nPACK=%llu\nUPLOAD=%llu\nWAIT=%llu\nTIMEOUTS=%llu\nPIXELS=%llu\nFACES=%llu\nITEMS=%llu\nHASH=%08x\nFRAMES=",names[p],phase_frames(p),
            (unsigned long long)packs[p],(unsigned long long)uploads[p],(unsigned long long)waits[p],
            (unsigned long long)timeouts[p],(unsigned long long)pixels[p],(unsigned long long)faces[p],
            (unsigned long long)items[p],hashes[p]);
        for(f=0;f<phase_frames(p);++f) fprintf(file,"%s%u",f?",":"",times[p][f]);
        fprintf(file,"\nSECTIONS=");
        for(s=0;s<10;++s) fprintf(file,"%s%llu",s?",":"",(unsigned long long)sections[p][s]);
        fprintf(file,"\nEDIT_CALLS=");
        for(f=0;f<phase_frames(p);++f) fprintf(file,"%s%u",f?",":"",edit_calls[p][f]);
        fprintf(file,"\n");
#ifdef DOS_VIEWMODEL
        fprintf(file,"VIEWMODEL_US=%llu\n",(unsigned long long)viewmodelTimes[p]);
#endif
#ifdef DOS_BENCH_PROFILE_FAR
        fprintf(file,"FAR_TIMES=%u,%u,%u\n",farTimes[p][0],farTimes[p][1],farTimes[p][2]);
#endif
#ifdef DOS_BENCH_PROFILE_CW
        fprintf(file,"CW_TIMES=");
        for(s=0;s<CW_N;++s) fprintf(file,"%s%llu",s?",":"",(unsigned long long)cw_times[p][s]);
        fprintf(file,"\nCW_CALLS=");
        for(s=0;s<CW_N;++s) fprintf(file,"%s%llu",s?",":"",(unsigned long long)cw_calls[p][s]);
        fprintf(file,"\nCW_REASONS=");
        for(s=0;s<CW_REASON_N;++s) fprintf(file,"%s%llu",s?",":"",(unsigned long long)cw_reasons[p][s]);
        fprintf(file,"\n");
#endif
#ifdef DOS_WALL_VISIBILITY
        fprintf(file,"WALL_SOURCES_TESTS_REJECTED_BY_FRAME=");
        for(f=0;f<phase_frames(p);++f) fprintf(file,"%s%u,%u,%u",f?";":"",wall_stats[p][f][0],wall_stats[p][f][1],wall_stats[p][f][2]);
        fprintf(file,"\n");
#endif
#ifdef DOS_BENCH_PROFILE_DRAW
        fprintf(file,"DRAW_SAMPLE_BEGIN=%u\nDRAW_SAMPLE_END=%u\n",profile_begin[p],profile_end[p]);
#endif
#ifdef DOS_FRONT_TO_BACK
        fprintf(file,"F2B_SAMPLE_SKIP_ROWS_HIDDEN_PRIMS_COVERED_FALLBACK_STOP=");
        for(s=0;s<8;++s) fprintf(file,"%s%u",s?",":"",f2b_stats[p][s]);
        fprintf(file,"\n");
#endif
    }
#ifdef DOS_COLLECT_VERIFY
    fprintf(file,"COLLECT_VERIFICATION=TIMINGS_INVALID\nCOLLECT_VERIFY_FRAMES=%u\nCOLLECT_VERIFY_FAILURES=%u\n",dos_collect_verify_frames,dos_collect_verify_failures);
    extern unsigned dos_collect_verify_poses[32][8];
    for(unsigned i=0;i<dos_collect_verify_failures && i<32;++i)
        fprintf(file,"COLLECT_FAIL_POSE=%u,%u,%u,%u,%u,%u,%u,%u\n",dos_collect_verify_poses[i][0],
            dos_collect_verify_poses[i][1],dos_collect_verify_poses[i][2],dos_collect_verify_poses[i][3],
            dos_collect_verify_poses[i][4],dos_collect_verify_poses[i][5],dos_collect_verify_poses[i][6],dos_collect_verify_poses[i][7]);
#endif
    if(ferror(file) || fclose(file)) exit(1);
    path[10]='E'; path[11]='N'; path[12]='D';
    file=fopen(path,"wb"); if(!file) exit(1);
    if(fprintf(file,"%s\n",ok?"PASS":"FAIL")<0 || fclose(file)) exit(1);
    if(!ok || !suite || run==3) {
        puts("DOScraft benchmark complete; safe to close this private VM."); exit(!ok);
    }
    restore_house();
    /* Restoration dirties meshes. Drain that work outside measured frames so
     * later repeats start with the same clean fixture as the first run. */
    while(world_update_dirty_chunks(1000)) {}
    world_stream(sx,sz-6,g_viewDist+8,1000);
    ++run;
    if(sweep) select_quality(run-1);
    clear_samples();
    phase=0; frame=0;
}
void bench_frame(void)
{
    extern u32 g_statPixels;
#ifdef DOS_BENCH_A1_CENSUS
    extern int render_a1_dump(int scene,int sample);
#endif
    unsigned int now=bench_us();
#ifdef DOS_WALL_VISIBILITY
    if(phase>=0 && frame>=WARM && frame<WARM+phase_frames(phase)) {
        wall_stats[phase][frame-WARM][0]=dos_wall_sources;
        wall_stats[phase][frame-WARM][1]=dos_wall_tests;
        wall_stats[phase][frame-WARM][2]=dos_wall_rejected;
    }
#endif
    if(measured) {
        int n=frame-WARM;
        bench_mark(S_PRESENT);
        times[phase][n]=now-last;
        packs[phase]+=dos_bench_pack; uploads[phase]+=dos_bench_upload;
        waits[phase]+=dos_bench_wait; timeouts[phase]+=dos_bench_timeouts;
        pixels[phase]+=g_statPixels; faces[phase]+=g_statFaces; items[phase]+=g_statItems;
#ifdef DOS_FRONT_TO_BACK
        record_f2b(phase);
#endif
    } else if(phase>=0 && frame==WARM+phase_frames(phase)-1) {
        /* Last frame was closed before readback. Include its real duration
         * and transport counters, but never the diagnostic readback time. */
        times[phase][phase_frames(phase)-1]=secmark-last;
        packs[phase]+=dos_bench_pack; uploads[phase]+=dos_bench_upload;
        waits[phase]+=dos_bench_wait; timeouts[phase]+=dos_bench_timeouts;
        pixels[phase]+=g_statPixels; faces[phase]+=g_statFaces; items[phase]+=g_statItems;
#ifdef DOS_FRONT_TO_BACK
        record_f2b(phase);
#endif
    }
#if defined(DOS_BENCH_DIAGONAL_ROUTE) && defined(DOS_BENCH_DUMP_FRAMES)
    if(phase==1 && run==3 && frame>=WARM && (frame-WARM)%4==0) {
#ifdef DOS_RASTER_FINISH_SKIP
        raster_complete_odd_rows();
#endif
#ifdef DOS_VERY_LOW
        dos_video_complete_very_low_source(g_fb,512);
#endif
        char path[]="C:\\D1F000.RAW"; int index=frame-WARM;
        path[4]=(char)('0'+run); path[6]=(char)('0'+index/100); path[7]=(char)('0'+(index/10)%10); path[8]=(char)('0'+index%10);
        bench_dump_frame(path,g_fb);
    }
#endif
    if(phase<0) {
        sx=g_player.body.x/FU; sz=g_player.body.z/FU; sy=world_surface_y(sx,sz);
#ifdef DOS_BENCH_QUALITY
        select_quality(sweep?0:DOS_BENCH_QUALITY);
#else
        select_quality(0);
#endif
#ifdef DOS_BENCH_OCCLUSION
        g_occlusion=1;
#endif
        phase=0; frame=-1;
    }
#ifdef DOS_BENCH_A1_CENSUS
    if(phase>=0 && frame>=WARM && run==1 && !suite_warmup) {
        int measured_frame=frame-WARM;
        int sample=measured_frame==0 ? 0 : measured_frame==FRAMES/3 ? 1 :
                   measured_frame==(2*FRAMES)/3 ? 2 : measured_frame==FRAMES-1 ? 3 : -1;
        if(sample>=0 && render_a1_dump(phase,sample)) vga_ok=0;
    }
#endif
    if(++frame==WARM+phase_frames(phase)) {
#ifdef DOS_BENCH_PROFILE_DRAW
        profile_end[phase]=g_profCount;
#endif
        frame=0; if(++phase==PHASES) finish();
        if(phase==4) house();
    }
#ifdef DOS_BENCH_PROFILE_DRAW
    if(!frame) profile_begin[phase]=g_profCount;
#endif
    g_time=6000; g_skyDarken=0;
    g_player.body.x=sx*FU+FU/2; g_player.body.z=(sz-6)*FU+FU/2;
    g_player.body.y=(sy+1)*FU; g_player.yaw=0; g_player.pitch=-40;
    /* Longer sampling repeats the same bounded route, not a progressively
     * longer walk into unrelated terrain. The legacy 32-frame route is exact. */
    int pose=frame<WARM?frame:WARM+(frame-WARM)%32;
#ifdef DOS_BENCH_DIAGONAL_ROUTE
    if(phase==1) pose=frame;
#endif
    if(phase>=1 && phase<=3) {
        int travelStep=FU/8;
#ifdef DOS_BENCH_TRAVEL_STEP
        if(phase==1) travelStep=DOS_BENCH_TRAVEL_STEP;
#endif
        g_player.body.x+=pose*travelStep; g_player.yaw=pose*8;
#ifdef DOS_BENCH_DIAGONAL_ROUTE
        g_player.body.z+=pose*travelStep; g_player.yaw=128;
#endif
        g_player.body.y=(world_surface_y(g_player.body.x/FU,g_player.body.z/FU)+1)*FU;
        g_player.pitch=phase==2?-250:phase==3?250:-40;
#ifdef DOS_BENCH_DIAGONAL_ROUTE
        if(phase==1) g_player.pitch=0;
#endif
    }
    if(phase>=4) {
        g_player.body.z=sz*FU+FU/2; g_player.body.y=(sy+1)*FU; g_player.pitch=-20;
        g_player.yaw=pose*16;
        if(phase==5) {
            unsigned int edit_start=bench_us();
            world_set(sx+2,sy+2,sz,(frame&1)?B_AIR:B_STONE);
            unsigned int duration=bench_us()-edit_start;
            if(frame>=WARM) edit_calls[phase][frame-WARM]=duration;
        }
    }
    g_player.body.vx=g_player.body.vy=g_player.body.vz=0;
#ifdef DOS_BENCH_VIEWMODEL_CASES
    g_player.selected=0; g_inv[0].item=run==1 ? 0 : run==2 ? B_COBBLE : I_IRON_PICK;
    g_inv[0].count=run==1 ? 0 : 1;
#endif
#ifdef DOS_VIEWMODEL
    dos_viewmodel_us=0;
#endif
    measured=frame>=WARM; dos_bench_pack=dos_bench_upload=dos_bench_wait=dos_bench_timeouts=0;
    g_statPixels=0;
#ifdef DOS_BENCH_PROFILE_FAR
    memset(dos_far_us,0,3*sizeof(u32));
    if(!frame && !phase) memset(farTimes,0,sizeof(farTimes));
#endif
    last=secmark=bench_us();
}
