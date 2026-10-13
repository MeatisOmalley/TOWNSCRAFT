#ifndef DOS_FAR_DDA_H
#define DOS_FAR_DDA_H
#include "common.h"

/* View depth uses 12 fractional bits. A cell interval ends on an actual
 * world-grid boundary, independent of the camera-relative sampling lattice. */
#define FAR_DDA_INF 0x40000000u
typedef struct {
    int x,z,shift,dx,dz;
    u32 tx,tz,sx,sz,t,end,detail,rx,rz;
} FarDda;
static inline u32 far_dda_cross(int boundary,int origin,int direction,u32 reciprocal)
{
    if(!direction) return FAR_DDA_INF;
    return (u32)(((unsigned long long)(unsigned)ABS(boundary-origin)*reciprocal)>>12);
}
static inline void far_dda_grid(FarDda *r,int eyeX,int eyeZ,int shift)
{
    int px=eyeX+((r->dx*(int)(r->t>>4))>>14);
    int pz=eyeZ+((r->dz*(int)(r->t>>4))>>14);
    r->shift=shift;
    r->x=(px-(r->dx<0))>>shift; r->z=(pz-(r->dz<0))>>shift;
    r->tx=far_dda_cross((r->x+(r->dx>0))*(1<<shift),eyeX,r->dx,r->rx);
    r->tz=far_dda_cross((r->z+(r->dz>0))*(1<<shift),eyeZ,r->dz,r->rz);
    r->sx=r->dx ? r->rx>>(12-shift) : FAR_DDA_INF;
    r->sz=r->dz ? r->rz>>(12-shift) : FAR_DDA_INF;
}
static inline u32 far_dda_exit(const FarDda *r)
{
    u32 end=MIN(r->tx,r->tz);
    if(r->shift==8) end=MIN(end,r->detail);
    return MIN(end,r->end);
}
static inline void far_dda_advance(FarDda *r,u32 end,int eyeX,int eyeZ)
{
    if(r->tx==end) { r->x+=r->dx>0 ? 1 : -1; r->tx+=r->sx; }
    if(r->tz==end) { r->z+=r->dz>0 ? 1 : -1; r->tz+=r->sz; }
    r->t=MAX(end,r->t);
    if(r->shift==8 && r->t>=r->detail) far_dda_grid(r,eyeX,eyeZ,9);
}
#endif
