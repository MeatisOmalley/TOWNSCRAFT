#ifndef DOS_FAR_FIELD_H
#define DOS_FAR_FIELD_H
#include "common.h"
#include "far_fill.h"
#define DOS_FAR_MAGIC 0x31465254u
#define DOS_FAR_BYTES (512u*512u*2u)
#define DOS_FAR_SECTOR (178291200u/512u)
extern int dos_far_enabled;
extern u32 dos_far_version;
extern u32 dos_far_boundary_version;
extern u8 *dos_far_map;
int dos_far_available(void);
void dos_far_toggle(void);
void dos_far_change(int direction);
int dos_far_sample(int x,int z,int detailed,u8 *block);
const u8 *dos_far_ready_flags(void);
void raster_far_strip(int x,int width,int y0,int y1,u8 color);
void raster_far_pattern(int x,int width,int y0,int y1,u8 a,u8 b,int phase);
#endif
