#ifndef DOS_BENCH_PROFILE_CW_H
#define DOS_BENCH_PROFILE_CW_H
#include "bench.h"

enum { CW_CAMERA, CW_FLOOR, CW_COLLECT, CW_SORT, CW_DIRTY, CW_STREAM,
       CW_MESH_SCAN, CW_MESH_PUBLISH, CW_CACHE_STREAM, CW_LIGHT,
       CW_COLUMN_PREPARE, CW_COLUMN_LOAD, CW_WRITE_POLL,
       CW_SKY_INIT, CW_LIGHT_SEED, CW_LIGHT_FLOOD, CW_N };
enum { CW_X, CW_Y, CW_Z, CW_MESH, CW_SCALE, CW_VIEW, CW_OCCLUSION,
       CW_YAW, CW_PITCH, CW_HIT, CW_REASON_N };
void bench_cw_time(int section,u32 us);
void bench_cw_reason(unsigned mask);

#endif
