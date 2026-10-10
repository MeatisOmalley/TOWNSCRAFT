#include <assert.h>
#include <stdio.h>
#include "wall_visibility.h"
static unsigned state=0x48ab3421;
static unsigned random32(void) { state=state*1664525u+1013904223u; return state; }
static int random_between(int a,int b) { return a+(int)(random32()%(unsigned)(b-a+1)); }

/* Independent double-precision ray/plane intersection oracle. */
static int oracle(const DosWallCone *w,const int lo[3],const int hi[3],const int cam[3],unsigned key)
{
    int k,a,cell[3]={0,0,0};
    if(key<=w->key) return 0;
    for(k=0;k<8;++k) {
        double point[3],depth,t;
        int n=0;
        for(a=0;a<3;++a) point[a]=((k&(1<<a))?hi[a]+16:lo[a]-16)-cam[a];
        depth=point[w->axis]*w->side;
        if(depth<=w->distance+32 || depth>DOS_WALL_LIMIT) return 0;
        t=w->distance/depth;
        for(a=0;a<3;++a) if(a!=w->axis) {
            double world=cam[a]+point[a]*t;
            int current=(int)world/256;
            if(k==0) cell[a]=current;
            if(point[a]<-DOS_WALL_LIMIT || point[a]>DOS_WALL_LIMIT ||
               point[a]*t<=w->lo[n] || point[a]*t>=w->hi[n] ||
               current!=cell[a] || world<=current*256+32 || world>=(current+1)*256-32) return 0;
            ++n;
        }
    }
    return 1;
}

int main(void)
{
    int i,accepted=0;
    for(i=0;i<100000;++i) {
        DosWallCone wall;
        int a,axis=i%3,cam[3],wl[3],wh[3],lo[3],hi[3],plane;
        unsigned key=random_between(0,30);
        for(a=0;a<3;++a) {
            cam[a]=random_between(10000,50000);
            wl[a]=cam[a]+random_between(-4000,2000); wh[a]=wl[a]+random_between(128,6000);
            lo[a]=cam[a]+random_between(-18000,18000); hi[a]=lo[a]+random_between(0,4000);
        }
        plane=cam[axis]+(i&1?1:-1)*random_between(128,2048);
        if(!dos_wall_cone(&wall,axis,plane,wl,wh,cam,10)) continue;
        assert(dos_wall_hidden(&wall,lo,hi,cam,key)==oracle(&wall,lo,hi,cam,key));
        accepted+=dos_wall_hidden(&wall,lo,hi,cam,key);
    }
    assert(accepted>10);
    {
        /* A wall split around a doorway must not hide anything in that gap.
           Camera looks from z=0 at a wall z=512. Target farther behind. */
        DosWallCone left,right;
        int cam[3]={0,384,0},wl[3]={-1024,0,512},wh[3]={-128,1024,512};
        int lo[3]={-32,320,1024},hi[3]={32,448,1088};
        assert(dos_wall_cone(&left,2,512,wl,wh,cam,1));
        wl[0]=128; wh[0]=1024;
        assert(dos_wall_cone(&right,2,512,wl,wh,cam,1));
        assert(!dos_wall_hidden(&left,lo,hi,cam,10));
        assert(!dos_wall_hidden(&right,lo,hi,cam,10));
        lo[0]=-700; hi[0]=-660; lo[1]=370;hi[1]=400;
        assert(dos_wall_hidden(&left,lo,hi,cam,10));
        assert(!dos_wall_hidden(&left,lo,hi,cam,1));
        assert(!dos_wall_cone(&left,3,512,wl,wh,cam,1));
        assert(!dos_wall_cone(&left,2,64,wl,wh,cam,1));
        assert(!dos_wall_cone(&left,2,4096,wl,wh,cam,1));
    }
    printf("PASS: 100000 independent ray/plane boxes; %d hidden; doorway remains visible\n",accepted);
    return 0;
}
