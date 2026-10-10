#include "render.h"
#include "world.h"
#include "fmath.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
Camera g_cam;
int g_viewDist,g_renderScale,g_W;
typedef struct { int c[3]; } V3;
#define NPLANES 5
#define NEAR_Z 12
#include "camera_reference.inc"
#include "split_gate.inc"
static unsigned int rng=92317;
static unsigned int next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }

static int leaf_rejected(int x,int y,int z,int dir)
{
    int x1=x+1,y1=y+1,z1=z+1;
    if(dir==DIR_NX) x1=x;
    else if(dir==DIR_PX) x=x1;
    else if(dir==DIR_NY) y1=y;
    else if(dir==DIR_PY) y=y1;
    else if(dir==DIR_NZ) z1=z;
    else if(dir==DIR_PZ) z=z1;
    return box_outside(x,x1,y,y1,z,z1);
}

int main(void)
{
    int i,k,x,z,dir,rejected=0,children=0;
    /* Negative control: weighted fixed-point planes need not be monotonic.
     * Endpoint-only parent rejection would incorrectly remove a visible child.
     */
    for(k=0;k<5;++k) for(i=0;i<258;++i)
        reference_px[k][i]=reference_pz[k][i]=-1000;
    for(k=0;k<5;++k) for(i=0;i<WH+2;++i) reference_py[k][i]=-1000;
    reference_px[1][0]=reference_px[1][2]=10; reference_px[1][1]=-10;
    reference_py[1][0]=reference_pz[1][0]=reference_pz[1][1]=0;
    assert(box_outside(0,2,0,0,0,1));
    assert(!leaf_rejected(0,0,0,DIR_NY));
    assert(split_parent_visible(0,0,0,DIR_NY,2,1));
    reference_px[1][1]=4;
    assert(!split_parent_visible(0,0,0,DIR_NY,2,1));

    for(i=0;i<20000;++i) {
        int bx,bz,by,ex,ez;
        g_W=256; g_viewDist=(i&1)?8:12; g_renderScale=1+(i&1);
        g_cam=(Camera){(32+(int)(next()%192))*256+(int)(next()%256),
            (3+(int)(next()%(WH-6)))*256+(int)(next()%256),
            (32+(int)(next()%192))*256+(int)(next()%256),
            (int)(next()%1024),(int)(next()%501)-250};
        reference_setup_camera();
        bx=g_cam.x>>8; bz=g_cam.z>>8; by=g_cam.y>>8;
        for(dir=0;dir<6;++dir) {
            ex=dir<=DIR_PX ? 1 : 1+(int)(next()%16);
            ez=dir<=DIR_PY ? 1+(int)(next()%16) : 1;
            x=bx-(int)(next()%ex); z=bz-(int)(next()%ez);
            int y=by+(int)(next()%7)-3,ix,iz;
            if(!split_parent_visible(x,y,z,dir,ex,ez)) {
                ++rejected;
                for(iz=0;iz<ez;++iz) for(ix=0;ix<ex;++ix) {
                    /* +/- face on the full parent's outer plane. */
                    int lx=x+ix,lz=z+iz;
                    if(dir==DIR_PX) lx=x+ex-1;
                    if(dir==DIR_PZ) lz=z+ez-1;
                    assert(leaf_rejected(lx,y,lz,dir)); ++children;
                }
            }
        }
    }
    assert(rejected>1000 && children>10000);
    printf("PASS: 120000 real parent gates, %d rejected parents, %d descendant checks\n",rejected,children);
    return 0;
}
