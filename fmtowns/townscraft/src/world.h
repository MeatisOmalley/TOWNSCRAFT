#ifndef WORLD_H
#define WORLD_H
#include "common.h"
#include "blocks.h"

#define WH 48            /* World height */
#define SEA_LEVEL 20
#define CS 16            /* Chunk size */
#define NCY (WH/CS)

extern int g_W;          /* World width and depth (multiple of 16) */
extern int g_NC;         /* Chunks per horizontal axis */
extern u8 *g_blocks;
extern u8 *g_light;      /* sky<<4 | block */
extern u8 *g_height;     /* Per column: 1 + highest light-blocking cell */
extern int *g_zOff;      /* z*W*WH */
/* NULL during legacy generation/reference tests; otherwise logical column -> resident slot. */
extern short *g_columnMap,*g_columnCoords;
extern u8 *g_columnReady;
extern int g_residentColumns,g_allocChunkCount;
#define COLUMN_CELLS (CS*CS*WH)
extern const u8 g_lightOpacity[NUM_BLOCKS];

/* Chunk meshes: greedy-merged quads of exposed faces, plus model cells.
   Each quad is two words:
     w0: lx 0-3, lz 4-7, ly 8-11, dir 12-14, model 15, (w-1) 16-19, (h-1) 20-23
     w1: texture 0-7, light byte of the cell in front 8-15, block 16-23
   Top/bottom quads extend w along x and h along z.  Z-facing quads are runs
   (w) along x, X-facing quads are runs (h) along z. */
/* Quads of a chunk are grouped by direction (DIR_*) and, within a
   direction, by the 8x8 quadrant (lx>=8 | (lz>=8)<<1) the quad starts in,
   so off-screen parts of a chunk can be skipped a group at a time. */
#define NSUB 4
#define NGROUPS (6*NSUB)
typedef struct
{
	u32 off;           /* in quads */
	u16 count,cap;
	u16 group[NGROUPS+1];  /* Quads per group (dir*NSUB+quadrant), then
	                          model cells.  Within a group, quads are sorted
	                          so the ones facing a camera on the near side
	                          come first. */
	u8 gbox[NGROUPS][3];   /* Cells covered by each group: x min|max<<4,
	                          z min|max<<4, y min|max<<4 (chunk local) */
	u8 dirty;          /* Immediate edits, or deferred light/stream rebuilds */
	u8 sealed;         /* Bottom layer only: cannot be seen into from above */
	u8 meshed;         /* Has a mesh (see world_stream) */
} Chunk;

enum
{
	DIRTY_LIGHT=1,
	DIRTY_GEOMETRY=2,
	DIRTY_STREAM=3
};

extern Chunk *g_chunks;
extern u32 *g_meshPool;    /* 2 words per quad */

#define MQ_LX(w) ((w)&15)
#define MQ_LZ(w) (((w)>>4)&15)
#define MQ_LY(w) (((w)>>8)&15)
#define MQ_DIR(w) (((w)>>12)&7)
#define MQ_MODEL 0x8000
#define MQ_W(w) ((((w)>>16)&15)+1)
#define MQ_H(w) ((((w)>>20)&15)+1)
#define MQ_TEX(w) ((w)&255)
#define MQ_LIGHT(w) (((w)>>8)&255)
#define MQ_BLK(w) ((u8)((w)>>16))

static inline int widx(int x,int y,int z)
{
	if(g_columnMap)
	{
		int slot=g_columnMap[(z/CS)*g_NC+x/CS];
		return slot<0 ? -1 : slot*COLUMN_CELLS+((z&15)*CS+(x&15))*WH+y;
	}
	return g_zOff[z]+x*WH+y;
}
static inline int in_world(int x,int y,int z)
{
	return (unsigned)x<(unsigned)g_W && (unsigned)z<(unsigned)g_W && (unsigned)y<WH;
}
static inline u8 wget(int x,int y,int z)
{
	if((unsigned)x>=(unsigned)g_W || (unsigned)z>=(unsigned)g_W || y<0)
	{
		return B_BEDROCK;
	}
	if(y>=WH)
	{
		return B_AIR;
	}
	{
		int i=widx(x,y,z);
		return i<0 ? B_BEDROCK : g_blocks[i];
	}
}
static inline int chunk_index(int cx,int cy,int cz)
{
	if(g_columnMap)
	{
		int slot=g_columnMap[cz*g_NC+cx];
		return (slot<0 ? g_residentColumns : slot)*NCY+cy;
	}
	return (cz*g_NC+cx)*NCY+cy;
}

static inline int height_index(int x,int z)
{
	if(g_columnMap)
	{
		int slot=g_columnMap[(z/CS)*g_NC+x/CS];
		return (slot<0 ? g_residentColumns : slot)*CS*CS+(z&15)*CS+(x&15);
	}
	return z*g_W+x;
}
static inline int world_column_ready(int cx,int cz)
{
	return cx>=0 && cz>=0 && cx<g_NC && cz<g_NC &&
	       (!g_columnMap || g_columnMap[cz*g_NC+cx]>=0);
}
static inline int world_column_sim_ready(int cx,int cz)
{
	return world_column_ready(cx,cz) && (!g_columnMap || !g_columnReady || g_columnReady[g_columnMap[cz*g_NC+cx]]);
}
static inline int world_height(int x,int z) { return g_height[height_index(x,z)]; }

void world_alloc(void);
void world_set_column_pin(int (*pin)(int,int));
void world_set_column_needed(int (*needed)(int *,int *));
int world_cache_active(void);
int world_store_flush(void);
u32 world_store_used(void);
int world_stream_error(void);
int world_save_columns(void (*put)(u32),u32 (*position)(void));
int world_load_columns(u32 (*get)(void),u32 (*position)(void));
int world_backing_bank(u32 bankBytes);
int world_materialize_columns(void);
int world_save_legacy_columns(void (*put)(int));
void world_commit_columns(void);
void world_set_backing_reader(int (*read)(u32,u32,u8 *));
void world_import_begin(void);
int world_import_end(int ok,int x,int z);
void world_load_position(int x,int z);
void world_generate(u32 seed);
void world_set(int x,int y,int z,u8 b);   /* Updates light and meshes */
int world_light_at(int x,int y,int z);    /* Raw light byte (sky<<4|block) */
int world_update_dirty_chunks(int maxLightOnly);  /* Returns work used; streamed rebuilds are incremental */
extern u32 g_meshQuads;
extern u32 g_meshVersion;
void world_rebuild_after_load(void);   /* Changes whenever a chunk mesh is rebuilt */

/* Mesh streaming: only chunks near the camera have meshes.  Builds up to
   maxBuild missing meshes within radius blocks of (x,z), nearest first,
   and frees the farthest meshes when the pool runs out. */
void world_stream(int x,int z,int radius,int maxBuild);
int world_surface_y(int x,int z);         /* y of first air above ground (spawn helper) */
int world_is_solid(int x,int y,int z);

extern int g_spawnX,g_spawnY,g_spawnZ;
extern void (*g_genProgress)(int percent);

#endif
