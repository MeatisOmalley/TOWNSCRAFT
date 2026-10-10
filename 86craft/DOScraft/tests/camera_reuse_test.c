#include "render.h"
#include "world.h"
#include "fmath.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
Camera g_cam;
int g_viewDist,g_renderScale,g_W;
typedef struct { int c[3]; } V3;
#include "camera_reference.inc"
#include "camera_candidate.inc"
static unsigned int rng=811;
static unsigned int next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
int main(void)
{
    int i,j;
    static const int widths[]={96,160,208,256};
    for(i=0;i<12000;++i) {
        if(i%4==0) {
            g_W=widths[next()%4]; g_viewDist=(next()%2)?8:12; g_renderScale=1+next()%2;
            g_cam=(Camera){(int)(next()%(g_W*256)),(int)(next()%(WH*256)),
                (int)(next()%(g_W*256)),(int)(next()%1024),(int)(next()%501)-250};
        } else if(i%4==1) {
            /* Identical pose, arbitrary mesh/world updates: tables still exact. */
        } else if(i%4==2) {
            /* One key at a time, including resolution and world boundaries. */
            switch((i/4)%8) {
            case 0:g_cam.x+=1;break;case 1:g_cam.y+=1;break;case 2:g_cam.z+=1;break;
            case 3:g_cam.yaw+=1;break;case 4:g_cam.pitch+=1;break;
            case 5:g_renderScale=3-g_renderScale;break;
            case 6:g_viewDist=g_viewDist==8?12:8;break;case 7:g_W=256;break;
            }
        } else if(i%17==0) {
            /* Reinitialization can replace/clear all buffers at the SAME pose. */
            reference_clear(); candidate_clear();
        }
        reference_setup_camera(); candidate_setup_camera();
        assert(!memcmp(reference_tx,candidate_tx,sizeof(reference_tx)));
        assert(!memcmp(reference_ty,candidate_ty,sizeof(reference_ty)));
        assert(!memcmp(reference_tz,candidate_tz,sizeof(reference_tz)));
        assert(!memcmp(reference_visx,candidate_visx,sizeof(reference_visx)));
        assert(!memcmp(reference_visy,candidate_visy,sizeof(reference_visy)));
        assert(!memcmp(reference_visz,candidate_visz,sizeof(reference_visz)));
        assert(!memcmp(reference_rot,candidate_rot,sizeof(reference_rot)));
        assert(!memcmp(reference_planeLen,candidate_planeLen,sizeof(reference_planeLen)));
        assert(!memcmp(reference_AX16,candidate_AX16,sizeof(reference_AX16)));
        assert(!memcmp(reference_AY16,candidate_AY16,sizeof(reference_AY16)));
        assert(!memcmp(reference_AZ16,candidate_AZ16,sizeof(reference_AZ16)));
        for(j=0;j<5;++j) {
            assert(!memcmp(reference_px[j],candidate_px[j],sizeof(reference_px[j])));
            assert(!memcmp(reference_py[j],candidate_py[j],sizeof(reference_py[j])));
            assert(!memcmp(reference_pz[j],candidate_pz[j],sizeof(reference_pz[j])));
        }
        assert(reference_camBX==candidate_camBX && reference_camBY==candidate_camBY && reference_camBZ==candidate_camBZ);
        assert(reference_tabX0==candidate_tabX0 && reference_tabX1==candidate_tabX1);
        assert(reference_tabZ0==candidate_tabZ0 && reference_tabZ1==candidate_tabZ1);
    }
    puts("PASS: 12000 exact camera/table comparisons including every invalidation key"); return 0;
}
