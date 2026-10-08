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
	BO_LOOK_FRAMES=14,   /* Standing, looking in 8 directions */
	BO_LOOK_SUM=15,
	BO_COMPACTS=10,      /* Mesh pool compactions (whole run) */
	BO_EVICTS=11,        /* Columns evicted from the mesh pool */
	BO_REBUILDS=12,      /* Chunk mesh rebuilds (partial or whole) */
	BO_LAYERS=13,        /* Layers meshed by those rebuilds */
	BO_EDIT=16,          /* Longest of the 3 frames after each edit */
	BO_EDIT_UPD=32,      /* Longest world update after each edit */
	BO_EDIT_COLLECT=48,  /* Longest face list collect+sort after each edit */
	BO_PHASE_COUNTS=64,  /* BO_COMPACTS..BO_LAYERS: 4 each at the end of
	                        generation, of the edits and of the walk */
	BO_COUNT=76
};

/* Frame sections timed in the look and walk phases (g_benchSec[phase][]) */
enum
{
	S_GAME,      /* Input, game ticks */
	S_UPDATE,    /* Mesh updates and streaming */
	S_COLLECT,   /* Camera setup, mob boxes, face list collect and sort */
	S_ENT,       /* Entity sorting */
	S_WAIT,      /* Waiting for the page flip */
	S_SKY,       /* Sky and clouds */
	S_DRAW,      /* Faces, models and entities */
	S_FINISH,    /* Target outline, row doubling */
	S_HUD,       /* Rain, HUD, menus */
	S_PRESENT,   /* Flip and the rest of the loop */
	S_FRAMES=10,
	S_PIXELS,    /* Texels written by the span loops */
	S_FACES,     /* Polygons passed to the rasterizer */
	S_ITEMS,     /* Draw list items */
	S_N=16
};

#ifdef BENCH_EDIT
extern u32 g_benchOut[BO_COUNT];
extern u32 g_benchSec[2][S_N];
void bench_mark(int section);
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
