/* Conservative world-space shadow cone of ONE opaque axis-aligned patch.
 * No filled bounding rectangles, screen masks or 64-bit math. Only a box
 * passing the coarse cone incurs a seam-cell division on each lateral axis.
 * Model/alpha semantics and actual publication are certified by the caller. */
#ifndef DOS_WALL_VISIBILITY_H
#define DOS_WALL_VISIBILITY_H
#define DOS_WALL_LIMIT 16384
typedef struct {
    int axis,side,distance,lo[2],hi[2];
    unsigned key;
} DosWallCone;

static int dos_wall_small(int x) { return x>=-DOS_WALL_LIMIT && x<=DOS_WALL_LIMIT; }

static int dos_wall_cone(DosWallCone *out,int axis,int plane,
                         const int lo[3],const int hi[3],const int camera[3],unsigned key)
{
    int a,n=0,d;
    if(axis<0 || axis>2) return 0;
    d=plane-camera[axis];
    if(!dos_wall_small(d) || d==0) return 0;
    out->axis=axis; out->side=d>0?1:-1; out->distance=d*out->side; out->key=key;
    /* Very close walls risk near clipping; far walls offer too little
       inset in screen pixels to certify fixed-point/raster edge rounding. */
    if(out->distance<128 || out->distance>2048) return 0;
    for(a=0;a<3;++a) if(a!=axis) {
        out->lo[n]=lo[a]+32-camera[a]; out->hi[n]=hi[a]-32-camera[a];
        if(out->lo[n]>=out->hi[n] || !dos_wall_small(out->lo[n]) || !dos_wall_small(out->hi[n])) return 0;
        ++n;
    }
    return 1;
}

static int dos_wall_hidden(const DosWallCone *wall,const int lo[3],const int hi[3],
                            const int camera[3],unsigned key)
{
    int a,n=0,depth0,depth1,lower[2],upper[2],axes[2];
    if(key<=wall->key) return 0; /* Every source draws AFTER the rejected item. */
    depth0=(lo[wall->axis]-16-camera[wall->axis])*wall->side;
    depth1=(hi[wall->axis]+16-camera[wall->axis])*wall->side;
    if(depth0>depth1) { int t=depth0; depth0=depth1; depth1=t; }
    if(depth0<=wall->distance+32 || depth1>DOS_WALL_LIMIT) return 0;
    for(a=0;a<3;++a) if(a!=wall->axis) {
        int low=lo[a]-16-camera[a],high=hi[a]+16-camera[a];
        int dl=wall->lo[n]<0?depth0:depth1;
        int dh=wall->hi[n]<0?depth1:depth0;
        if(!dos_wall_small(low) || !dos_wall_small(high) ||
           low*wall->distance<=wall->lo[n]*dl ||
           high*wall->distance>=wall->hi[n]*dh) return 0;
        lower[n]=low;upper[n]=high;axes[n]=a;
        ++n;
    }
    /* Imported raster/subdivision can leave a doubled-pixel crack on a
       shared cell edge. Certify ONE inset grid cell, not a seamless union.
       The representative ray only chooses a candidate cell; exact bounded
       products below certify every box corner against that cell's interior. */
    for(n=0;n<2;++n) {
        int cell=(camera[axes[n]]+lower[n]*wall->distance/depth0)>>8;
        int low=cell*256+32-camera[axes[n]],high=(cell+1)*256-32-camera[axes[n]];
        int dl=low<0?depth0:depth1,dh=high<0?depth1:depth0;
        if(!dos_wall_small(low) || !dos_wall_small(high) ||
           lower[n]*wall->distance<=low*dl || upper[n]*wall->distance>=high*dh) return 0;
    }
    return 1;
}
#endif
