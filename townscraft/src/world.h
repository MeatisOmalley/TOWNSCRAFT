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
	u8 dirty;          /* DIRTY_GEOMETRY or DIRTY_LIGHT */
} Chunk;

enum
{
	DIRTY_LIGHT=1,
	DIRTY_GEOMETRY=2
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
	return g_blocks[widx(x,y,z)];
}
static inline int chunk_index(int cx,int cy,int cz)
{
	return (cz*g_NC+cx)*NCY+cy;
}

void world_alloc(void);
void world_generate(u32 seed);
void world_set(int x,int y,int z,u8 b);   /* Updates light and meshes */
int world_light_at(int x,int y,int z);    /* Raw light byte (sky<<4|block) */
void world_update_dirty_chunks(int maxLightOnly);  /* Geometry changes always rebuild */
extern u32 g_meshQuads;
int world_surface_y(int x,int z);         /* y of first air above ground (spawn helper) */
int world_is_solid(int x,int y,int z);

extern int g_spawnX,g_spawnY,g_spawnZ;
extern void (*g_genProgress)(int percent);

#endif
