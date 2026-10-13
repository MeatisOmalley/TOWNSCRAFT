#ifndef DOS_FAR_FIELD_H
#define DOS_FAR_FIELD_H
#include "common.h"
#include "blocks.h"
#include "far_fill.h"
#define DOS_FAR_MAGIC 0x31465254u
#define DOS_FAR_BYTES (512u*512u*2u)
#define DOS_FAR_SECTOR (178291200u/512u)
extern int dos_far_enabled;
extern u32 dos_far_version;
extern u32 dos_far_boundary_version;
extern u8 *dos_far_map;
#define DOS_TREE_MAGIC 0x31525446u
#define DOS_TREE_SECTOR (178815488u/512u)
#define DOS_LOG_PLACED 64
typedef struct { u32 mask,first; } FarTreeGroup;
typedef struct { u16 x,z; u8 top,base,active,reserved; } FarTree;
extern FarTreeGroup *dos_far_tree_groups;
extern u8 *dos_far_under;
extern u32 *dos_far_tree_columns;
extern FarTree *dos_far_trees;
extern u32 dos_far_tree_count,dos_far_ground_version;
static inline u32 dos_far_popcount(u32 v) {
    v-=((v>>1)&0x55555555u);v=(v&0x33333333u)+((v>>2)&0x33333333u);
    v=(v+(v>>4))&0x0f0f0f0fu;return (v*0x01010101u)>>24;
}
static inline int dos_far_under_index(u32 cell) {
    if(!dos_far_tree_groups) return -1;
    const FarTreeGroup *g=&dos_far_tree_groups[cell>>5];u32 bit=1u<<(cell&31);
    return (g->mask&bit) ? (int)(g->first+dos_far_popcount(g->mask&(bit-1))) : -1;
}
static inline int dos_far_ground_cell(u32 cell,u8 *block) {
    const u8 *p=dos_far_map+cell*2;
    if(BLK_ID(p[1])==B_LEAVES && dos_far_under) {
        int index=dos_far_under_index(cell);
        if(index>=0) p=dos_far_under+index*2;
        else { *block=B_AIR;return 0; }
    }
    *block=BLK_ID(p[1]);return p[0];
}
int dos_far_available(void);
void dos_far_toggle(void);
void dos_far_change(int direction);
int dos_far_sample(int x,int z,int detailed,u8 *block);
const u8 *dos_far_ready_flags(void);
void raster_far_strip(int x,int width,int y0,int y1,u8 color);
void raster_far_pattern(int x,int width,int y0,int y1,u8 a,u8 b,int phase);
#endif
