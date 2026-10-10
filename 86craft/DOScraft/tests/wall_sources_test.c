/* Exercise actual source selection, rectangle union and publication checks. */
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef unsigned u32;
#define CS 16
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
enum { IK_QUAD,IK_MODEL,IK_BOX };
typedef struct { u32 a,b; } Item;
typedef struct { int meshed,dirty,dirtyLayers,trimmed; } Chunk;
static Item items[32];
static unsigned short order[32];
static unsigned itemKey[32];
static int nStatic,nWallPatches,ready[4];
static unsigned char texTransparent[256];
static Chunk g_chunks[12];
static int world_column_sim_ready(int x,int z) { return x>=0 && x<2 && z>=0 && z<2 && ready[z*2+x]; }
static int chunk_index(int x,int y,int z) { return (z*2+x)*3+y; }
#include "wall_sources.inc"

static void add(int x,int y,int z,int dir,int ex,int ez,int tex,int kind)
{
    int i=nStatic++;
    assert(i<32);
    items[i].a=x|z<<8|y<<16|dir<<22|kind<<25;
    items[i].b=tex|(ex-1)<<16|(ez-1)<<20;
    order[i]=i; itemKey[i]=i+1;
}
int main(void)
{
    int i;
    for(i=0;i<4;++i) ready[i]=1;
    for(i=0;i<12;++i) g_chunks[i].meshed=1;
    /* Left/right wall pillars around a two-block doorway: never bridge it. */
    for(i=0;i<4;++i) { add(4,4+i,4,4,2,1,0,IK_QUAD); add(8,4+i,4,4,2,1,0,IK_QUAD); }
    wall_select(); assert(nWallPatches==2);
    for(i=0;i<nWallPatches;++i) {
        assert(wallPatches[i].lo[1]==4*256 && wallPatches[i].hi[1]==8*256);
        assert(wallPatches[i].hi[0]<=6*256 || wallPatches[i].lo[0]>=8*256);
        assert(wall_patch_ready(&wallPatches[i]));
    }
    g_chunks[0].dirty=2;
    for(i=0;i<nWallPatches;++i) assert(!wall_patch_ready(&wallPatches[i]));
    wall_select(); assert(!nWallPatches);
    g_chunks[0].dirty=0;
    /* Publication/alpha/model exclusions, including mesh truncation. */
    texTransparent[1]=1; nStatic=0;
    add(4,4,4,4,2,1,1,IK_QUAD); add(4,4,4,4,2,1,0,IK_MODEL);
    wall_select(); assert(!nWallPatches);
    nStatic=0; add(4,4,4,4,2,1,0,IK_QUAD);
    g_chunks[0].trimmed=1; wall_select(); assert(!nWallPatches);
    g_chunks[0].trimmed=0; ready[0]=0; wall_select(); assert(!nWallPatches);
    ready[0]=1; wall_select(); assert(nWallPatches==1);
    /* A merged patch crossing chunks checks ALL source owners on reuse. */
    nStatic=0; add(12,4,4,4,4,1,0,IK_QUAD); add(16,4,4,4,4,1,0,IK_QUAD);
    wall_select(); assert(nWallPatches==1 && wall_patch_ready(&wallPatches[0]));
    g_chunks[3].dirtyLayers=1;
    assert(!wall_patch_ready(&wallPatches[0]));
    puts("PASS: actual opaque source union preserves doorways and rechecks every dirty/resident owner");
    return 0;
}
