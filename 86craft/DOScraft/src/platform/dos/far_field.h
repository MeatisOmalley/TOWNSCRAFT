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
#define DOS_FAR_CAP_MAGIC 0x31504143u
#define DOS_FAR_HEIGHT_MASK 63
#define DOS_LOG_PLACED 64
extern int dos_far_caps;
/* The world is 48 blocks high; the height byte's upper two bits are spare. */
static inline int dos_far_capped_height(const u8 *p,int h)
{
    if(!dos_far_caps || !h || BLK_ID(p[h-1])!=B_LOG || (p[h-1]&DOS_LOG_PLACED)) return h;
    int leaves=0,logs=0;
    while(h+leaves<48 && leaves<3 && BLK_ID(p[h+leaves])==B_LEAVES) ++leaves;
    if(!leaves) return h;
    for(int y=h-1;y>=0 && BLK_ID(p[y])==B_LOG && !(p[y]&DOS_LOG_PLACED);--y) ++logs;
    return logs>=4 && logs<=6 ? h|(leaves<<6) : h;
}
int dos_far_available(void);
void dos_far_toggle(void);
void dos_far_change(int direction);
int dos_far_sample(int x,int z,int detailed,u8 *block);
const u8 *dos_far_ready_flags(void);
void raster_far_strip(int x,int width,int y0,int y1,u8 color);
void raster_far_pattern(int x,int width,int y0,int y1,u8 a,u8 b,int phase);
#endif
