#ifndef BENCH_H
#define BENCH_H
#include "common.h"

#define BENCH_EDITS 16
#define BENCH_WALK_SEC 30

/* g_benchOut layout (all times in microseconds unless noted) */
enum
{
	BO_DONE=0,
	BO_GEN_MS=1,         /* World generation, milliseconds */
	BO_IDLE_FRAMES=2,    /* Frames between edits (not following one) */
	BO_IDLE_SUM=3,
	BO_WALK_FRAMES=4,
	BO_WALK_SUM=5,
	BO_WALK_MAX=6,
	BO_WALK_OVER66=7,    /* Frames longer than 66ms */
	BO_WALK_OVER100=8,
	BO_WALK_UPD_MAX=9,   /* Longest world update (meshes) in one frame */
	BO_EDIT=16,          /* Longest of the 3 frames after each edit */
	BO_EDIT_UPD=32,      /* Longest world update after each edit */
	BO_EDIT_COLLECT=48,  /* Longest face list collect+sort after each edit */
	BO_COUNT=64
};

#ifdef BENCH_EDIT
extern u32 g_benchOut[BO_COUNT];
u32 bench_us(void);
void bench_gen_begin(void);
void bench_gen_end(void);
void bench_frame(void);
void bench_update_begin(void);
void bench_update_end(void);
void bench_collect(u32 us);
#define BENCH_HOOK(x) x
#else
#define BENCH_HOOK(x)
#endif

#endif
